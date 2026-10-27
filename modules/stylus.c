/* stylus.c: userstyles for web sites, for the Stylus browser extension. The firefox
 * module themes the browser; this one themes the pages, through Stylus, which runs
 * in Firefox and the Chromium family alike.
 *
 * Every <site>.user.css in templates/stylus/<set>/ is rendered into
 * ~/.config/themer/stylus/<site>.user.css, a UserCSS file with its metadata block
 * (@name, @version, @updateURL, @-moz-document ...). Stylus cannot be written to
 * from outside the browser, so the style is installed once by hand, from
 * themer-web: open http://localhost:PORT/stylus/youtube.user.css in the browser
 * and Stylus offers to install it. Its @updateURL is that address and its
 * @version grows with every apply, so Stylus's update check brings every later
 * apply in without a tab kept open ("Live reload" on the install page does it at
 * once while that page stays open). themer-web is started from the main menu of
 * any front end (Server), or with themer server start. The template set ships
 * youtube.user.css and wikipedia.user.css; a set
 * adds a site by adding a file, nothing in the module changes.
 *
 * Options (after the host's mode):
 *   font      ui or terminal: the sites' text in the theme's font; site keeps each
 *             site's own
 *   <site>    on or off, one per site (youtube, ...): off leaves that site alone
 *   template  the set under templates/stylus/ */
#include <dirent.h>
#include <limits.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "themer_module.h"

#define DIR_NAME "stylus"
#define MAX_SITES 32
#define NVARS     6   /* template_vars: font_family .. image_filter */

static const TmOption opts[] = {
	{ "font", "font for the sites' text: ui or terminal from the theme, or site to keep each site's own", "ui|terminal|site", "ui", 0, 0 },
	TM_OPT_TEMPLATE,
};

/* One on|off option per site, named after its file (youtube for youtube.user.css),
 * for every site any template set has: a site that is off is not rendered, and its
 * file is removed on the next apply. */
static char site_names[MAX_SITES][TM_NAME_LEN], site_descs[MAX_SITES][112];

static int
site_options(TmOption *o, int max)
{
	char files[MAX_SITES][TM_NAME_LEN];
	int nf = tm_template_names(DIR_NAME, files, MAX_SITES), n = 0;

	for (int i = 0; i < nf && n < max; i++) {
		size_t len = strlen(files[i]);

		if (len < 10 || strcmp(files[i] + len - 9, ".user.css"))
			continue;
		snprintf(site_names[n], sizeof site_names[n], "%.*s", (int)(len - 9), files[i]);
		snprintf(site_descs[n], sizeof site_descs[n], "restyle %.40s (%.50s)", site_names[n], files[i]);
		o[n] = (TmOption){ site_names[n], site_descs[n], "on|off", "on", 0, 0 };
		n++;
	}
	return n;
}

/* whether a site file's option is on (sites without an option are on) */
static int
site_on(const TmTheme *t, const char *file)
{
	char key[TM_NAME_LEN + 16];
	const char *v;

	snprintf(key, sizeof key, "stylus.%.*s", (int)(strlen(file) - 9), file);
	v = tm_opt(t, key);
	return !v || strcmp(v, "off");
}


/* the module's own variables: font_family and font_size for the chosen font;
 * font_rule, a complete declaration or nothing when the site keeps its own;
 * stylus_url, where themer-web serves the style (for @updateURL), with the site's
 * file name appended by the template; version, a number that grows with every
 * apply (1.<days>.<minutes of the day>), so Stylus's update check sees a newer
 * style each time; image_filter, the filter that turns black-on-transparent drawings
 * (formulas, diagrams) light on a dark page, or none in light mode */
static int
template_vars(const TmTheme *t, TmVar *v, int max)
{
	static char fam[128], size_s[16], rule[256], url[128], version[32], filter[64];
	const char *font = tm_opt(t, "stylus.font");
	time_t now = time(NULL);
	int size;

	if (max < NVARS)
		return 0;
	snprintf(url, sizeof url, "http://localhost:%d/stylus", t->web_port);
	snprintf(version, sizeof version, "1.%ld.%ld", (long)(now / 86400), (long)(now % 86400) / 60);
	tm_font_for(t, "stylus.font", fam, sizeof fam, &size);
	snprintf(size_s, sizeof size_s, "%d", size);
	rule[0] = '\0';
	if (!font || strcmp(font, "site"))
		snprintf(rule, sizeof rule, "font-family: \"%s\", sans-serif !important;", fam);
	v[0] = (TmVar){ "font_family", fam };
	v[1] = (TmVar){ "font_size", size_s };
	v[2] = (TmVar){ "font_rule", rule };
	v[3] = (TmVar){ "stylus_url", url };
	v[4] = (TmVar){ "version", version };
	snprintf(filter, sizeof filter, "%s", t->dark ? "invert(1) hue-rotate(180deg)" : "none");
	v[5] = (TmVar){ "image_filter", filter };
	return NVARS;
}

TM_VARS(template_vars)

/* the sites of a set: every *.user.css under templates/stylus/<set>/, plus those of
 * the built-in set, so a set that adds one site still gets the rest */
static int
sites(const TmTheme *t, char names[][64], int max)
{
	const char *sets[2] = { tm_opt(t, "stylus.template"), TM_DEFAULT_SET };
	int n = 0;

	for (int s = 0; s < 2 && n < max; s++) {
		char dir[PATH_MAX];
		DIR *d;
		struct dirent *e;

		if (!sets[s] || !*sets[s] || tm_template_dir(DIR_NAME, sets[s], dir, sizeof dir) < 0)
			continue;
		if (!(d = opendir(dir)))
			continue;
		while ((e = readdir(d)) && n < max) {
			size_t len = strlen(e->d_name);
			int dup = 0;

			if (len < 10 || len >= 64 || strcmp(e->d_name + len - 9, ".user.css"))
				continue;
			for (int i = 0; i < n; i++)
				if (!strcmp(names[i], e->d_name))
					dup = 1;
			if (!dup)
				memcpy(names[n++], e->d_name, len + 1);
		}
		closedir(d);
	}
	return n;
}

static int
present(const TmTheme *t)
{
	static const char *const browsers[] = {
		"firefox", "librewolf", "zen", "floorp", "chromium", "chromium-browser",
		"google-chrome", "google-chrome-stable", "brave", "brave-browser", "vivaldi"
	};
	char p[PATH_MAX];

	for (size_t i = 0; i < TM_COUNT(browsers); i++)
		if (tm_have(browsers[i]))
			return 1;
	tm_path(p, sizeof p, t, ".mozilla");
	return !access(p, F_OK);
}

static int
load(const TmTheme *t)
{
	static char body[65536];
	char names[MAX_SITES][64], p[PATH_MAX], rel[PATH_MAX];
	TmVar v[NVARS];
	int nv = template_vars(t, v, TM_COUNT(v));
	int n = sites(t, names, MAX_SITES);

	for (int i = 0; i < n; i++) {
		if (!site_on(t, names[i]))
			continue;
		if (tm_render(body, sizeof body, DIR_NAME, tm_opt(t, "stylus.template"), names[i],
		              t, v, nv) < 0)
			return -1;
		snprintf(rel, sizeof rel, ".config/themer/stylus/%s", names[i]);
		tm_path(p, sizeof p, t, rel);
		if (tm_writef(p, "%s", body) < 0)
			return -1;
	}
	for (int i = 0, k = n; i < k; i++)
		if (!site_on(t, names[i]))
			n--;
	if (n)
		fprintf(stderr, "themer: stylus: %d userstyle%s in ~/.config/themer/stylus/, served at "
		                "http://localhost:%d/stylus/ (install once from there)\n", n, n == 1 ? "" : "s", t->web_port);
	return 0;
}

static const TmModule mod = {
	.name = "stylus",
	.desc = "Stylus: web sites (YouTube, Wikipedia) in the theme, through the browser extension",
	.templates = DIR_NAME,
	.default_on = 1,
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
	.more_options = site_options,
};
TM_EXPORT(mod)
