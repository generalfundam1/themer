/* system.c: the root side of the console palette and font, GRUB's colors and ly's.
 *
 *   sudo themer system install [USER]   once: a systemd unit that keeps them in step
 *   sudo themer system remove [USER]    everything back as it was, the unit gone
 *   themer system sync USER             what the unit runs (root): put USER's files
 *                                       into effect
 *   themer system status [USER]
 *
 * Every apply writes, as the user, what these should be into
 * ~/.config/themer/system/ (see targets.c and modules/ly.c). The unit is a path unit
 * watching that directory: when a file there changes, the sync runs as root and
 * applies it. It runs at boot too, so the console comes up in the theme. Each file
 * in /etc it changes is copied to /etc/themer/backup/ the first time, and put back
 * when the user's file goes away (Remove all styles, the ly module off) and on
 * remove. Without systemd, install says so and syncs once; run the sync from the
 * init system's own hook to keep it current. */
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "themer.h"

#define ETC_DIR    "/etc/themer"
#define BACKUP_DIR ETC_DIR "/backup"
#define UNIT_DIR   "/etc/systemd/system"

static int
user_home(const char *user, char *home, size_t n, uid_t *uid, gid_t *gid)
{
	struct passwd *pw = getpwnam(user);

	if (!pw)
		return -1;
	snprintf(home, n, "%s", pw->pw_dir);
	*uid = pw->pw_uid;
	*gid = pw->pw_gid;
	return 0;
}

static int
exists(const char *p)
{
	return access(p, F_OK) == 0;
}

static char *
slurp(const char *path)
{
	FILE *f = fopen(path, "rb");
	char *t;
	long n;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	rewind(f);
	if (n < 0 || !(t = malloc((size_t)n + 1))) {
		fclose(f);
		return NULL;
	}
	if (fread(t, 1, (size_t)n, f) != (size_t)n) {
		free(t);
		fclose(f);
		return NULL;
	}
	t[n] = '\0';
	fclose(f);
	return t;
}

static int
put(const char *path, const char *text)
{
	return write_bytes(path, text, strlen(text));
}

/* the backup of a file in /etc: its path with / as _, or that with .absent when the
 * file did not exist (then restoring deletes it) */
static void
backup_name(const char *path, char *out, size_t n, int absent)
{
	char flat[PATH_MAX - 64];

	snprintf(flat, sizeof flat, "%s", path);
	for (char *p = flat; *p; p++)
		if (*p == '/')
			*p = '_';
	snprintf(out, n, "%s/%s%s", BACKUP_DIR, flat, absent ? ".absent" : "");
}

static void
backup(const char *path)
{
	char b[PATH_MAX], a[PATH_MAX], *t;

	backup_name(path, b, sizeof b, 0);
	backup_name(path, a, sizeof a, 1);
	if (exists(b) || exists(a))
		return;                     /* the first one is the original */
	mkdirs(BACKUP_DIR);
	if ((t = slurp(path))) {
		put(b, t);
		free(t);
	} else {
		put(a, "");
	}
}

/* puts a file back as it was before the first sync; 1 when it did */
static int
restore(const char *path)
{
	char b[PATH_MAX], a[PATH_MAX], *t;

	backup_name(path, b, sizeof b, 0);
	backup_name(path, a, sizeof a, 1);
	if ((t = slurp(b))) {
		put(path, t);
		free(t);
		unlink(b);
		return 1;
	}
	if (exists(a)) {
		unlink(path);
		unlink(a);
		return 1;
	}
	return 0;
}

/* a program in the usual places (the unit runs with a short PATH) */
static int
find_prog(const char *name, char *out, size_t n)
{
	static const char *const dirs[] = { "/usr/bin", "/usr/sbin", "/bin", "/sbin", "/usr/local/bin", "/usr/local/sbin" };

	for (size_t i = 0; i < TM_COUNT(dirs); i++)
		if (snprintf(out, n, "%s/%s", dirs[i], name) < (int)n && !access(out, X_OK))
			return 0;
	return -1;
}

static int
run_prog(const char *prog, const char *arg1, const char *arg2, const char *arg3)
{
	char p[PATH_MAX], cmd[PATH_MAX * 3];

	if (find_prog(prog, p, sizeof p) < 0)
		return -1;
	snprintf(cmd, sizeof cmd, "'%s'%s%s%s%s%s%s >/dev/null 2>&1", p,
	         arg1 ? " " : "", arg1 ? arg1 : "", arg2 ? " " : "", arg2 ? arg2 : "",
	         arg3 ? " " : "", arg3 ? arg3 : "");
	return run(cmd);
}

/* the value of KEY=VALUE in one of the user's files, unquoted as written */
static int
user_value(const char *file, const char *key, char *out, size_t n)
{
	char *t = slurp(file), *l, *save = NULL;
	size_t kl = strlen(key);
	int found = 0;

	if (!t)
		return -1;
	for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save))
		if (!strncmp(l, key, kl) && l[kl] == '=') {
			snprintf(out, n, "%s", l + kl + 1);
			found = 1;
		}
	free(t);
	return found ? 0 : -1;
}

/* ---- ly: "key = value" in config.ini, "key = value," in the ly table of config.lua ---- */

static const char *
ly_config(void)
{
	return exists("/etc/ly/config.lua") ? "/etc/ly/config.lua" :
	       exists("/etc/ly/config.ini") ? "/etc/ly/config.ini" : NULL;
}

/* sets key in ly's config; a missing key is added (before the table's end in Lua) */
static int
ly_set(const char *path, const char *key, const char *value)
{
	char *t = slurp(path), *out, line[256];
	size_t kl = strlen(key), cap, o = 0;
	int lua = strstr(path, ".lua") != NULL, done = 0;
	const char *s, *close = NULL;

	if (!t)
		return -1;
	snprintf(line, sizeof line, lua ? "    %s = %s," : "%s = %s", key, value);
	cap = strlen(t) + strlen(line) + 4;
	out = malloc(cap);
	for (s = t; *s; ) {
		const char *e = strchr(s, '\n'), *k = s;
		size_t len = e ? (size_t)(e - s) : strlen(s);

		while (*k == ' ' || *k == '\t')
			k++;
		if (!strncmp(k, key, kl) && (k[kl] == ' ' || k[kl] == '\t' || k[kl] == '=')) {
			o += (size_t)snprintf(out + o, cap - o, "%s\n", line);
			done = 1;
		} else {
			if (lua && len && s[0] == '}')
				close = out + o;     /* the ly = { ... } table's last line */
			memcpy(out + o, s, len);
			o += len;
			out[o++] = '\n';
		}
		s += len + (e ? 1 : 0);
	}
	out[o] = '\0';
	if (!done) {
		if (lua && close) {
			size_t at = (size_t)(close - out), ll = strlen(line) + 1;

			memmove(out + at + ll, out + at, o - at + 1);
			memcpy(out + at, line, ll - 1);
			out[at + ll - 1] = '\n';
		} else if (!lua) {
			snprintf(out + o, cap - o, "%s\n", line);
		}
	}
	put(path, out);
	free(out);
	free(t);
	return 0;
}

/* ---- the sync ---- */

static int
sync_user(const char *user)
{
	char home[PATH_MAX - 64], dir[PATH_MAX - 32], f[PATH_MAX], v[256];
	uid_t uid;
	gid_t gid;
	int dir_ok, changed;
	const char *ly;

	if (user_home(user, home, sizeof home, &uid, &gid) < 0) {
		fprintf(stderr, "themer: no user %s\n", user);
		return 1;
	}
	snprintf(dir, sizeof dir, "%s/.config/themer/system", home);
	/* at boot the home may not be there yet (not mounted, not decrypted): then the
	 * palette kept in /etc is used, and nothing is taken for removed. A home that is
	 * there without the directory is one whose styles were removed. */
	{
		char cfgdir[PATH_MAX];

		snprintf(cfgdir, sizeof cfgdir, "%s/.config", home);
		dir_ok = exists(cfgdir);
	}

	/* the console palette */
	snprintf(f, sizeof f, "%s/tty.palette", dir);
	if (exists(f)) {
		char *t = slurp(f);

		if (t) {
			mkdirs(ETC_DIR);
			put(ETC_DIR "/tty.palette", t);
			free(t);
		}
	} else if (dir_ok && exists(ETC_DIR "/tty.palette")) {
		unlink(ETC_DIR "/tty.palette");
		run_prog("setvtrgb", "vga", NULL, NULL);
	}
	if (exists(ETC_DIR "/tty.palette"))
		run_prog("setvtrgb", ETC_DIR "/tty.palette", NULL, NULL);
	if (!dir_ok)
		return 0;

	/* the console font */
	snprintf(f, sizeof f, "%s/console.conf", dir);
	if (user_value(f, "FONT", v, sizeof v) == 0 && v[0]) {
		backup("/etc/vconsole.conf");
		kv_set_file("/etc/vconsole.conf", "FONT", v);
	} else {
		restore("/etc/vconsole.conf");
	}

	/* GRUB's colors, and grub.cfg made again only when they changed */
	snprintf(f, sizeof f, "%s/grub.conf", dir);
	changed = 0;
	if (exists("/etc/default/grub") && exists(f)) {
		char *before = slurp("/etc/default/grub"), *after;

		backup("/etc/default/grub");
		if (user_value(f, "GRUB_COLOR_NORMAL", v, sizeof v) == 0)
			kv_set_file("/etc/default/grub", "GRUB_COLOR_NORMAL", v);
		if (user_value(f, "GRUB_COLOR_HIGHLIGHT", v, sizeof v) == 0)
			kv_set_file("/etc/default/grub", "GRUB_COLOR_HIGHLIGHT", v);
		after = slurp("/etc/default/grub");
		changed = !before || !after || strcmp(before, after);
		free(before);
		free(after);
	} else if (!exists(f)) {
		changed = restore("/etc/default/grub");
	}
	if (changed) {
		if (exists("/boot/grub/grub.cfg"))
			run_prog("grub-mkconfig", "-o", "/boot/grub/grub.cfg", NULL);
		else if (exists("/boot/grub2/grub.cfg"))
			run_prog("grub2-mkconfig", "-o", "/boot/grub2/grub.cfg", NULL);
	}

	/* ly's colors */
	snprintf(f, sizeof f, "%s/ly.conf", dir);
	if ((ly = ly_config())) {
		char *t = slurp(f), *l, *save = NULL;

		if (t) {
			backup(ly);
			for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
				char *eq = strchr(l, '=');

				if (l[0] == '#' || !eq)
					continue;
				*eq = '\0';
				ly_set(ly, l, eq + 1);
			}
			free(t);
		} else {
			restore(ly);
		}
	}
	return 0;
}

/* ---- install, remove, status ---- */

static void
unit_names(const char *user, char *svc, char *path, size_t n)
{
	snprintf(svc, n, UNIT_DIR "/themer-system-%s.service", user);
	snprintf(path, n, UNIT_DIR "/themer-system-%s.path", user);
}

static int
install(const char *user)
{
	char home[PATH_MAX - 64], dir[PATH_MAX - 32], self[PATH_MAX], svc[PATH_MAX], pth[PATH_MAX], text[4 * PATH_MAX];
	uid_t uid;
	gid_t gid;
	ssize_t len;

	if (user_home(user, home, sizeof home, &uid, &gid) < 0) {
		fprintf(stderr, "themer: no user %s\n", user);
		return 1;
	}
	snprintf(dir, sizeof dir, "%s/.config/themer/system", home);
	if (!exists(dir)) {                 /* made the user's, for their applies to write */
		mkdirs(dir);
		if (chown(dir, uid, gid) < 0)
			perror("themer: chown");
	}
	if ((len = readlink("/proc/self/exe", self, sizeof self - 1)) < 0)
		snprintf(self, sizeof self, "/usr/local/bin/themer");
	else
		self[len] = '\0';
	if (!exists("/run/systemd/system")) {
		printf("no systemd here: synced once. To keep the console, GRUB and ly in step, run\n"
		       "  %s system sync %s\nas root from your init system after each apply (and at boot).\n", self, user);
		return sync_user(user);
	}
	unit_names(user, svc, pth, sizeof svc);
	snprintf(text, sizeof text,
	         "# generated by themer\n[Unit]\nDescription=themer: the console, GRUB and ly in %s's theme\n"
	         "After=local-fs.target\n\n[Service]\nType=oneshot\nExecStart=%s system sync %s\n\n"
	         "[Install]\nWantedBy=multi-user.target\n", user, self, user);
	put(svc, text);
	snprintf(text, sizeof text,
	         "# generated by themer\n[Unit]\nDescription=themer: watch %s's theme for the console, GRUB and ly\n\n"
	         "[Path]\nPathChanged=%s\nPathModified=%s\nUnit=themer-system-%s.service\n\n"
	         "[Install]\nWantedBy=paths.target\n", user, dir, dir, user);
	put(pth, text);
	run("systemctl daemon-reload");
	snprintf(text, sizeof text, "systemctl enable themer-system-%s.service >/dev/null 2>&1 && "
	         "systemctl enable --now themer-system-%s.path >/dev/null 2>&1", user, user);
	if (run(text) < 0)
		fprintf(stderr, "themer: systemctl could not enable the units\n");
	printf("installed: the console palette and font, GRUB's colors and ly follow %s's theme\n"
	       "(themer system remove puts them back)\n", user);
	return sync_user(user);
}

static int
uninstall(const char *user)
{
	char svc[PATH_MAX], pth[PATH_MAX], cmd[PATH_MAX];
	const char *ly = ly_config();

	unit_names(user, svc, pth, sizeof svc);
	if (exists(svc) || exists(pth)) {
		snprintf(cmd, sizeof cmd, "systemctl disable --now themer-system-%s.path themer-system-%s.service "
		         ">/dev/null 2>&1", user, user);
		run(cmd);
		unlink(svc);
		unlink(pth);
		run("systemctl daemon-reload");
	}
	restore("/etc/vconsole.conf");
	if (restore("/etc/default/grub")) {
		if (exists("/boot/grub/grub.cfg"))
			run_prog("grub-mkconfig", "-o", "/boot/grub/grub.cfg", NULL);
		else if (exists("/boot/grub2/grub.cfg"))
			run_prog("grub2-mkconfig", "-o", "/boot/grub2/grub.cfg", NULL);
	}
	if (ly)
		restore(ly);
	if (exists(ETC_DIR "/tty.palette")) {
		unlink(ETC_DIR "/tty.palette");
		run_prog("setvtrgb", "vga", NULL, NULL);
	}
	rmdir(BACKUP_DIR);
	rmdir(ETC_DIR);
	printf("removed: the console, GRUB and ly are as they were\n");
	return 0;
}

static int
status(const char *user)
{
	char svc[PATH_MAX], pth[PATH_MAX];

	unit_names(user, svc, pth, sizeof svc);
	printf("unit:     %s\n", exists(pth) ? "installed" : exists("/run/systemd/system") ? "not installed (sudo themer system install)" : "no systemd");
	printf("palette:  %s\n", exists(ETC_DIR "/tty.palette") ? ETC_DIR "/tty.palette" : "the console's own");
	printf("ly:       %s\n", ly_config() ? ly_config() : "not installed");
	return 0;
}

int
system_cmd(int argc, char **argv)
{
	const char *sub = argc > 2 ? argv[2] : "status";
	const char *user = argc > 3 ? argv[3] : getenv("SUDO_USER");

	if (!user || !*user) {
		struct passwd *pw = getpwuid(getuid());

		user = pw ? pw->pw_name : getenv("USER");
	}
	if (!user || !*user) {
		fprintf(stderr, "themer: whose theme? themer system %s USER\n", sub);
		return 1;
	}
	if (!strcmp(sub, "status"))
		return status(user);
	if (strcmp(sub, "install") && strcmp(sub, "remove") && strcmp(sub, "sync")) {
		fprintf(stderr, "usage: themer system install|remove|sync|status [USER]\n");
		return 1;
	}
	if (getuid() != 0) {
		fprintf(stderr, "themer: system %s changes files in /etc: run it with sudo\n", sub);
		return 1;
	}
	if (!strcmp(sub, "install"))
		return install(user);
	if (!strcmp(sub, "remove"))
		return uninstall(user);
	return sync_user(user);
}
