/* mozprofile.h: finds the profile a Mozilla application (Firefox, Thunderbird) is
 * using, for modules that write into its chrome/ directory. Shared by
 * modules/firefox.c and modules/thunderbird.c.
 *
 * The profile in use is the one a running instance holds: Mozilla marks it with a
 * "lock" symlink whose target ends in ":PID". If none is running, the profile used
 * most recently is chosen, judged by when prefs.js was last saved. userChrome.css
 * and userContent.css are read at startup only, so a running instance must be
 * restarted after a change. */
#ifndef MOZPROFILE_H
#define MOZPROFILE_H

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "themer_module.h"

/* the default profile's directory under base, from its profiles.ini (absolute or relative) */
static int
profile_dir(const char *base, char *out, size_t n)
{
	char ini[PATH_MAX], line[PATH_MAX], section[64] = "", install[PATH_MAX] = "", first[PATH_MAX] = "";
	const char *rel;
	FILE *f;

	if (snprintf(ini, sizeof ini, "%s/profiles.ini", base) >= (int)sizeof ini)
		return -1;
	if (!(f = fopen(ini, "r")))
		return -1;
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] == '[') {
			snprintf(section, sizeof section, "%.63s", line);
		} else if (!strncmp(section, "[Install", 8) && !strncmp(line, "Default=", 8)) {
			snprintf(install, sizeof install, "%s", line + 8);
		} else if (!strncmp(section, "[Profile", 8) && !strncmp(line, "Path=", 5) && !first[0]) {
			snprintf(first, sizeof first, "%s", line + 5);
		}
	}
	fclose(f);
	rel = install[0] ? install : first;
	if (!rel[0])
		return -1;
	if (rel[0] == '/')
		snprintf(out, n, "%s", rel);
	else
		if (snprintf(out, n, "%s/%s", base, rel) >= (int)n)
			return -1;
	return 0;
}

/* True if a running instance holds this profile. Firefox marks a profile in use
 * with a "lock" symlink whose target ends in ":PID" (or ":+PID"). The lock is the
 * reliable signal: a normal launch passes no -profile argument. */
static int
profile_in_use(const char *dir)
{
	char lock[PATH_MAX], target[PATH_MAX];
	ssize_t len;
	char *colon;
	long pid;

	snprintf(lock, sizeof lock, "%s/lock", dir);
	len = readlink(lock, target, sizeof target - 1);
	if (len <= 0)
		return 0;
	target[len] = '\0';
	colon = strrchr(target, ':');
	if (!colon)
		return 0;
	pid = strtol(colon + 1 + (colon[1] == '+'), NULL, 10);
	return pid > 0 && kill(pid, 0) == 0;
}


/* Finds the profile in use among the profile bases (directories holding a
 * profiles.ini, relative to HOME). Writes its directory to out and returns 1; prints
 * where it looked and returns 0 when there is none. app names the application in
 * messages, e.g. "firefox". */
static int
moz_profile(const TmTheme *t, const char *app, const char *const *bases, size_t nbases,
            char *out, size_t n)
{
	char dirs[8][PATH_MAX], base[PATH_MAX];
	size_t found = 0, pick = 0;
	int running = 0;

	for (size_t i = 0; i < nbases && found < 8; i++) {
		tm_path(base, sizeof base, t, bases[i]);
		if (profile_dir(base, dirs[found], sizeof dirs[found]) == 0)
			found++;
	}
	if (!found) {
		/* not an error, but say so: otherwise nothing shows that nothing was written */
		fprintf(stderr, "themer: %s: no profile found; looked in", app);
		for (size_t i = 0; i < nbases; i++) {
			tm_path(base, sizeof base, t, bases[i]);
			fprintf(stderr, " %s", base);
		}
		fprintf(stderr, "\n");
		return 0;
	}
	for (size_t i = 0; i < found && !running; i++)
		if (profile_in_use(dirs[i])) {
			pick = i;
			running = 1;
		}
	if (!running) {
		time_t newest = 0;

		for (size_t i = 0; i < found; i++) {
			char prefs[PATH_MAX + 16];
			struct stat st;

			snprintf(prefs, sizeof prefs, "%s/prefs.js", dirs[i]);
			if (!stat(prefs, &st) && st.st_mtime > newest) {
				newest = st.st_mtime;
				pick = i;
			}
		}
	}
	snprintf(out, n, "%s", dirs[pick]);
	fprintf(stderr, "themer: %s profile: %s (%s)\n", app, out,
	        running ? "running; restart it to apply" : "not running; start it to apply");
	return 1;
}

/* Writes the two marked blocks a Mozilla module produces: chrome into
 * chrome/userChrome.css and content into chrome/userContent.css of the profile. */
/* Writes the chrome and content blocks into the profile and copies the set's
 * assets (setdir, from tm_template_dir, or NULL for none) into chrome/. */
static int
moz_write(const char *profile, const char *id, const char *chrome, const char *content,
          const char *setdir)
{
	char css[PATH_MAX], cid[64];

	if (snprintf(css, sizeof css, "%s/chrome/userChrome.css", profile) >= (int)sizeof css ||
	    tm_blockf(css, id, "/*", " */", 0, "%s", chrome) < 0)
		return -1;
	snprintf(cid, sizeof cid, "%s-content", id);
	if (snprintf(css, sizeof css, "%s/chrome/userContent.css", profile) >= (int)sizeof css ||
	    tm_blockf(css, cid, "/*", " */", 0, "%s", content) < 0)
		return -1;
	/* the set's assets (images, fonts the stylesheets refer to), copied into chrome/,
	 * where url() in userChrome.css resolves; the templates themselves are not */
	if (setdir) {
		static const char *const skip[] = {
			"userChrome.css", "userContent.css", "tokens.css", "chrome.css", "content.css", "set.conf", NULL
		};

		if (snprintf(css, sizeof css, "%s/chrome", profile) >= (int)sizeof css ||
		    tm_copy_tree(setdir, css, skip) < 0)
			return -1;
	}
	return 0;
}

#endif
