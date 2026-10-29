/* gtk.c: GTK 2, 3, and 4, and the settings nwg-look covers.
 *
 * The set's gtk/ directory becomes a GTK theme, written to ~/.config/themer/gtk/ like
 * the qt module's files; ~/.themes/<set> is a link to it, so GTK finds it under the
 * set's name (themer_default, labelled "Themer Default" from set.conf): every
 * top-level .css file is rendered into gtk-3.0/ and gtk-4.0/ (gtk.css serves both; a
 * gtk-dark.css is what GTK loads in dark mode, as in any theme; a gtk4.css or
 * gtk4-dark.css is GTK 4's own instead of the shared one), and everything else in
 * the directory (assets/, images, fonts) is copied beside them as it is, so a theme
 * converted from an existing one keeps its url() references. GTK 3 and GTK 4 select
 * the theme through settings.ini (gtk-theme-name), never GTK_THEME: that variable
 * overrides the file and goes stale when the set changes (GTK 4 only for a set with
 * GTK 4 files of its own; see has_gtk4). libadwaita programs load
 * no named theme, only ~/.config/gtk-4.0/gtk.css: a marked block at its top imports
 * the set's libadwaita.css (colors for libadwaita's own look) or, without one, its
 * whole gtk.css.
 *
 * The settings (theme, icon theme, cursor theme and size, font, dark mode, and
 * the font rendering: antialiasing, hinting, subpixel order) go everywhere GTK
 * programs read them, which is what nwg-look writes too:
 *   ~/.config/gtk-3.0/settings.ini, ~/.config/gtk-4.0/settings.ini
 *   ~/.gtkrc-2.0                      GTK 2 programs
 *   ~/.config/xsettingsd/xsettingsd.conf   the XSETTINGS daemon, for X11 sessions
 *                                     (sent SIGHUP to re-read when it runs)
 *   gsettings org.gnome.desktop.interface  what GTK 4 and portal-aware programs
 *                                     read under a desktop portal (option gsettings)
 * A GTK app keeps the theme it loaded at launch, so running apps need a restart. */
#include <dirent.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "themer_module.h"

static const TmOption opts[] = {
	{ "font", "font for the GTK interface", "ui|terminal", "ui", 0, 0 },
	{ "gtk2", "theme GTK 2 programs (~/.gtkrc-2.0)", "on|off", "on", 0, 0 },
	{ "gtk3", "theme GTK 3 programs (the theme directory, settings.ini)", "on|off", "on", 0, 0 },
	{ "gtk4", "theme GTK 4 programs (the theme directory, settings.ini, the gtk-4.0/gtk.css import)", "on|off", "on", 0, 0 },
	{ "gsettings", "also set the gsettings keys (dconf), as nwg-look does", "on|off", "on", 0, 0 },
	{ "xsettingsd", "also write xsettingsd.conf, for X11 sessions", "on|off", "on", 0, 0 },
	TM_OPT_TEMPLATE,
};

/* the Xft hint style name for the hinting setting */
static const char *
hintstyle(const TmTheme *t)
{
	return !strcmp(t->hinting, "none")   ? "hintnone"
	     : !strcmp(t->hinting, "medium") ? "hintmedium"
	     : !strcmp(t->hinting, "full")   ? "hintfull" : "hintslight";
}

/* an on|off option, on unless set to off */
static int
on(const TmTheme *t, const char *key)
{
	const char *v = tm_opt(t, key);

	return !v || strcmp(v, "off");
}

/* the set the theme is drawn from */
static const char *
set_name(const TmTheme *t)
{
	const char *set = tm_opt(t, "gtk.template");

	return set && *set ? set : TM_DEFAULT_SET;
}

/* the GTK theme's name: the set's label (set.conf's name, "Themer Default"), else the
 * set's own name, so what GTK and the tools that show its theme report is the set's
 * name as the front ends show it, and switching sets switches themes. A '/' or a
 * quote in a label would end the name in a path or in gtkrc's and xsettingsd's
 * quotes: those become '-'. */
static const char *
theme_name(const TmTheme *t)
{
	static char label[128];

	if (tm_set_info(set_name(t), "name", label, sizeof label) < 0 || !label[0])
		snprintf(label, sizeof label, "%s", set_name(t));
	for (char *c = label; *c; c++)
		if (*c == '/' || *c == '"' || *c == '\\' || (unsigned char)*c < 0x20)
			*c = '-';
	if (label[0] == '.')
		label[0] = '-';
	return label;
}

/* whether a GTK 2 theme of that name is installed (a gtk-2.0 directory in it) */
static int
gtk2_theme_exists(const TmTheme *t, const char *name)
{
	char p[PATH_MAX + 300], home[PATH_MAX];
	const char *const dirs[] = { home, "/usr/share/themes", "/usr/local/share/themes" };

	if (!*name || strchr(name, '/'))
		return 0;
	for (int i = 0; i < 2; i++) {
		snprintf(home, sizeof home, "%s/%s", t->home, i ? ".local/share/themes" : ".themes");
		snprintf(p, sizeof p, "%s/%s/gtk-2.0", home, name);
		if (!access(p, F_OK))
			return 1;
	}
	for (size_t i = 1; i < sizeof dirs / sizeof dirs[0]; i++) {
		snprintf(p, sizeof p, "%s/%s/gtk-2.0", dirs[i], name);
		if (!access(p, F_OK))
			return 1;
	}
	return 0;
}

/* GTK 2 has no themer theme. A gtk-theme-name in ~/.gtkrc-2.0 that names no installed
 * GTK 2 theme (left by another tool, or a theme since removed) does nothing but
 * mislabel the theme, so it becomes the set's name, which GTK 2 resolves to its
 * default all the same; unload puts the old line back. A name of a real GTK 2 theme
 * is the user's choice and stays. */
static int
gtk2_name(const TmTheme *t, const char *path)
{
	FILE *f = fopen(path, "r");
	char line[512], cur[256] = "", val[160];
	int found = 0;

	if (!f)
		return 0;
	while (fgets(line, sizeof line, f)) {
		char *s = line, *q, *e;

		while (*s == ' ' || *s == '\t')
			s++;
		if (strncmp(s, "gtk-theme-name", 14))
			continue;
		s += 14;
		while (*s == ' ' || *s == '\t')
			s++;
		if (*s != '=')
			continue;
		if ((q = strchr(s, '"')) && (e = strchr(q + 1, '"'))) {
			snprintf(cur, sizeof cur, "%.*s", (int)(e - q - 1), q + 1);
			found = 1;
		}
	}
	fclose(f);
	if (!found || !strcmp(cur, theme_name(t)) || gtk2_theme_exists(t, cur))
		return 0;
	snprintf(val, sizeof val, "\"%s\"", theme_name(t));
	return tm_ini(path, "", "gtk-theme-name", val);
}

static void
settings_ini(const TmTheme *t, const char *rel, const char *font, int theme_key)
{
	char p[PATH_MAX], num[16];

	tm_path(p, sizeof p, t, rel);
	if (theme_key)
		tm_ini(p, "Settings", "gtk-theme-name", theme_name(t));
	tm_ini(p, "Settings", "gtk-icon-theme-name", t->icons);
	tm_ini(p, "Settings", "gtk-cursor-theme-name", t->cursor);
	snprintf(num, sizeof num, "%d", t->cursor_size);
	tm_ini(p, "Settings", "gtk-cursor-theme-size", num);
	tm_ini(p, "Settings", "gtk-font-name", font);
	tm_ini(p, "Settings", "gtk-application-prefer-dark-theme", t->dark ? "1" : "0");
	tm_ini(p, "Settings", "gtk-xft-antialias", t->antialias ? "1" : "0");
	tm_ini(p, "Settings", "gtk-xft-hinting", strcmp(t->hinting, "none") ? "1" : "0");
	tm_ini(p, "Settings", "gtk-xft-hintstyle", hintstyle(t));
	tm_ini(p, "Settings", "gtk-xft-rgba", t->rgba);
}

/* One gsettings key, run without a shell; output is dropped since there may be no
 * session bus. A string value is passed as a GVariant string literal, its quotes
 * and backslashes escaped, so a font or theme name cannot break out of it. */
static int
gset(const char *key, int is_string, const char *value)
{
	char lit[512];
	size_t o = 0;
	const char *argv[] = { "gsettings", "set", "org.gnome.desktop.interface", key, lit, NULL };

	if (is_string) {
		lit[o++] = '\'';
		for (const char *p = value; *p && o + 3 < sizeof lit; p++) {
			if (*p == '\'' || *p == '\\')
				lit[o++] = '\\';
			lit[o++] = *p;
		}
		lit[o++] = '\'';
		lit[o] = '\0';
	} else {
		snprintf(lit, sizeof lit, "%s", value);
	}
	return tm_run(argv);
}

/* whether there is a session bus: the address in the environment, or the
 * systemd user bus at $XDG_RUNTIME_DIR/bus, which D-Bus uses when the variable is
 * unset (a sway started from a console, for one) */
static int
session_bus(void)
{
	const char *rt = getenv("XDG_RUNTIME_DIR");
	char p[PATH_MAX];
	struct stat st;

	if (getenv("DBUS_SESSION_BUS_ADDRESS"))
		return 1;
	return rt && snprintf(p, sizeof p, "%s/bus", rt) < (int)sizeof p && stat(p, &st) == 0 && S_ISSOCK(st.st_mode);
}

/* gsettings keeps no undo of its own and unload cannot see a written key: the
 * values the keys had before themer first set them are kept in
 * ~/.config/themer/gtk-gsettings.saved (themer's own directory, not a recorded
 * file, so it outlives the cleanup that runs before each load) and put back by the
 * unload hook, which runs on unload and before every load. */
static const char *const gkeys[] = {
	"gtk-theme", "icon-theme", "cursor-theme", "cursor-size", "font-name",
	"monospace-font-name", "color-scheme", "font-antialiasing", "font-hinting",
	"font-rgba-order",
};

static void
saved_path(char *p, size_t n)
{
	const char *home = getenv("HOME");

	snprintf(p, n, "%s/.config/themer/gtk-gsettings.saved", home ? home : ".");
}

/* the current values, once: an existing file already holds the user's own */
static void
gsettings_save(void)
{
	char p[PATH_MAX], cmd[160], val[512];
	FILE *out, *in;
	int n = 0;

	saved_path(p, sizeof p);
	if (!access(p, F_OK) || !(out = fopen(p, "w")))
		return;
	for (size_t i = 0; i < sizeof gkeys / sizeof gkeys[0]; i++) {
		/* the command is fixed text: no user value reaches the shell */
		snprintf(cmd, sizeof cmd, "gsettings get org.gnome.desktop.interface %s 2>/dev/null", gkeys[i]);
		if (!(in = popen(cmd, "r")))
			continue;
		if (fgets(val, sizeof val, in)) {
			val[strcspn(val, "\n")] = '\0';
			if (val[0] && fprintf(out, "%s\t%s\n", gkeys[i], val) > 0)
				n++;
		}
		pclose(in);
	}
	fclose(out);
	if (!n)
		unlink(p);      /* nothing read (no schema, no answer): nothing to put back */
}

static void
gsettings(const TmTheme *t, const char *font)
{
	char mono[160], num[16];
	const char *aa = !t->antialias ? "none" : strcmp(t->rgba, "none") ? "rgba" : "grayscale";
	const int str = 1, raw = 0;

	snprintf(mono, sizeof mono, "%s %d", t->mono_font, t->mono_size);
	snprintf(num, sizeof num, "%d", t->cursor_size);
	gset("gtk-theme", str, theme_name(t));
	gset("icon-theme", str, t->icons);
	gset("cursor-theme", str, t->cursor);
	gset("cursor-size", raw, num);
	gset("font-name", str, font);
	gset("monospace-font-name", str, mono);
	/* the light side says so outright: "default" means no preference, and a
	 * program (Firefox, through the portal) is then free to pick dark. Schemas
	 * before GNOME 47 have no prefer-light; there "default" is the closest. */
	if (t->dark)
		gset("color-scheme", str, "prefer-dark");
	else if (gset("color-scheme", str, "prefer-light") != 0)
		gset("color-scheme", str, "default");
	gset("font-antialiasing", str, aa);
	gset("font-hinting", str, t->hinting);
	/* the key has no "none" (that is font-antialiasing=grayscale, set above) */
	if (strcmp(t->rgba, "none"))
		gset("font-rgba-order", str, t->rgba);
}

/* Earlier builds rendered the whole theme into ~/.config/gtk-3.0/gtk.css. A user
 * stylesheet outranks the theme, so one left there keeps feeding its colors to any
 * program that reads the named colors (@theme_base_color and so on) whatever the
 * theme now says. If the file is one of ours, move it aside. */
static void
retire_old_css(const TmTheme *t)
{
	char p[PATH_MAX], old[PATH_MAX + 12], line[128];
	FILE *f;

	tm_path(p, sizeof p, t, ".config/gtk-3.0/gtk.css");
	if (!(f = fopen(p, "r")))
		return;
	line[0] = '\0';
	if (!fgets(line, sizeof line, f))
		line[0] = '\0';
	fclose(f);
	if (!strstr(line, "generated by themer"))
		return;
	snprintf(old, sizeof old, "%s.themer-old", p);
	if (rename(p, old) == 0)
		fprintf(stderr, "themer: gtk: moved an old themer gtk.css aside: %s\n", old);
}

/* Whether the set has GTK 4 files of its own (gtk4*.css), set by theme_dirs. Only
 * then is GTK 4 given the set by name: a gtk.css written for both versions leaves
 * things to the theme beneath it (GTK 4 has no built-in check marks, as GTK 3 has),
 * so there GTK 4 keeps its default theme and takes the set's gtk.css on top, through
 * gtk-4.0/gtk.css. */
static int has_gtk4;

/* Where a top-level .css of the set's gtk/ goes: GTK 3 and GTK 4 (0), GTK 3 only
 * (3), or GTK 4 only (4), and under which name. A "gtk4" file is GTK 4's version of
 * the "gtk" file of the same name (gtk4.css is GTK 4's gtk.css, gtk4-dark.css its
 * gtk-dark.css) and keeps the shared one out of gtk-4.0; libadwaita.css is GTK 4's
 * alone. */
static int
css_target(const char *name, char *out, size_t n, char *const *all, int nall)
{
	if (!strncmp(name, "gtk4", 4)) {
		snprintf(out, n, "gtk%s", name + 4);
		return 4;
	}
	snprintf(out, n, "%s", name);
	if (!strcmp(name, "libadwaita.css"))
		return 4;
	if (!strncmp(name, "gtk", 3))
		for (int k = 0; k < nall; k++)
			if (!strncmp(all[k], "gtk4", 4) && !strcmp(all[k] + 4, name + 3))
				return 3;
	return 0;
}

/* The theme directories: every top-level .css of the set's gtk/ rendered into
 * gtk-3.0/ and gtk-4.0/ (css_target), the rest of the directory copied into both. */
static int
theme_dirs(const TmTheme *t)
{
	static char css[1024 * 1024];
	static const char *const versions[] = { "gtk-3.0", "gtk-4.0" };
	const char *set = tm_opt(t, "gtk.template");
	char dir[PATH_MAX], p[PATH_MAX], dst[PATH_MAX];
	char *names[64];
	int nnames = 0, r = -1;
	DIR *d;
	struct dirent *e;

	if (tm_template_dir("gtk", set, dir, sizeof dir) < 0) {
		/* a set without a gtk directory: the default set's, file by file */
		if (tm_template_dir("gtk", NULL, dir, sizeof dir) < 0)
			return -1;
	}
	if (!(d = opendir(dir)))
		return -1;
	while ((e = readdir(d)) && nnames < 63) {
		size_t len = strlen(e->d_name);

		if (e->d_name[0] == '.' || len < 5 || strcmp(e->d_name + len - 4, ".css"))
			continue;
		if (!(names[nnames] = strdup(e->d_name)))
			break;
		nnames++;
	}
	closedir(d);
	names[nnames] = NULL;
	has_gtk4 = 0;
	for (int i = 0; i < nnames; i++)
		has_gtk4 |= !strncmp(names[i], "gtk4", 4);
	for (int i = 0; i < nnames; i++) {
		char out[NAME_MAX + 1];
		int only = css_target(names[i], out, sizeof out, names, nnames);

		if (tm_render(css, sizeof css, "gtk", set, names[i], t, NULL, 0) < 0)
			goto done;
		for (int v = 0; v < 2; v++) {
			if (!on(t, v ? "gtk.gtk4" : "gtk.gtk3") || only == (v ? 3 : 4))
				continue;
			snprintf(p, sizeof p, ".config/themer/gtk/%s/%s", versions[v], out);
			tm_path(dst, sizeof dst, t, p);
			if (tm_writef(dst, "%s", css) < 0)
				goto done;
		}
	}
	for (int v = 0; v < 2; v++) {
		if (!on(t, v ? "gtk.gtk4" : "gtk.gtk3"))
			continue;
		snprintf(p, sizeof p, ".config/themer/gtk/%s", versions[v]);
		tm_path(dst, sizeof dst, t, p);
		if (tm_copy_tree(dir, dst, (const char *const *)names) < 0)
			goto done;
	}
	r = 0;
done:
	for (int i = 0; i < nnames; i++)
		free(names[i]);
	return r;
}

/* What ~/.config/gtk-4.0/gtk.css imports: libadwaita.css when the set has one (a
 * set that leaves libadwaita's look alone and only recolors it), else the whole
 * gtk.css, so libadwaita programs, which never load a named theme, follow the set. */
static const char *
gtk4_user_css(const TmTheme *t)
{
	char p[PATH_MAX];

	tm_path(p, sizeof p, t, ".config/themer/gtk/gtk-4.0/libadwaita.css");
	return access(p, R_OK) == 0 ? "libadwaita.css" : "gtk.css";
}

static int
load(const TmTheme *t)
{
	char p[PATH_MAX], font[128], fam[128];
	const char *gs = tm_opt(t, "gtk.gsettings"), *xs = tm_opt(t, "gtk.xsettingsd");
	int size;

	if (theme_dirs(t) < 0)
		return -1;
	if (on(t, "gtk.gtk3") || on(t, "gtk.gtk4")) {
		static const char *const parts[] = { "index.theme", "gtk-3.0", "gtk-4.0" };

		tm_path(p, sizeof p, t, ".config/themer/gtk/index.theme");
		if (tm_writef(p, "# generated by themer\n[Desktop Entry]\nType=X-GNOME-Metatheme\n"
		                 "Name=%s\nComment=a themer template set\n[X-GNOME-Metatheme]\n"
		                 "GtkTheme=%s\nIconTheme=%s\nCursorTheme=%s\n",
		              theme_name(t), theme_name(t), t->icons, t->cursor) < 0)
			return -1;
		/* where GTK looks for themes: ~/.themes/<the set's label>/, its parts links
		 * to themer's files. gtk-4.0 only for a set with GTK 4 files of its own:
		 * without one GTK 4 finds no GTK 4 theme there and keeps its own look, under
		 * the gtk.css that ~/.config/gtk-4.0/gtk.css imports, rather than loading
		 * CSS written for GTK 3 in its place */
		for (size_t i = 0; i < sizeof parts / sizeof parts[0]; i++) {
			char rel[PATH_MAX], target[PATH_MAX];

			if ((i == 2 && (!has_gtk4 || !on(t, "gtk.gtk4"))) || (i == 1 && !on(t, "gtk.gtk3")))
				continue;
			snprintf(rel, sizeof rel, ".config/themer/gtk/%s", parts[i]);
			tm_path(target, sizeof target, t, rel);
			snprintf(rel, sizeof rel, ".themes/%s/%s", theme_name(t), parts[i]);
			tm_path(p, sizeof p, t, rel);
			if (tm_symlink(target, p) < 0)
				return -1;
		}
	}
	retire_old_css(t);
	tm_path(p, sizeof p, t, ".config/gtk-4.0/gtk.css");
	if (on(t, "gtk.gtk4") &&
	    tm_blockf(p, "gtk", "/*", " */", 1,
	              "@import url(\"file://%s/.config/themer/gtk/gtk-4.0/%s\");\n", t->home,
	              gtk4_user_css(t)) < 0)
		return -1;

	tm_font_for(t, "gtk.font", fam, sizeof fam, &size);
	if (snprintf(font, sizeof font, "%s %d", fam, size) >= (int)sizeof font)
		return -1;

	if (on(t, "gtk.gtk3"))
		settings_ini(t, ".config/gtk-3.0/settings.ini", font, 1);
	if (on(t, "gtk.gtk4"))
		settings_ini(t, ".config/gtk-4.0/settings.ini", font, 1);

	/* GTK 2 has no themer theme; it gets the icons, cursor, font, and rendering */
	tm_path(p, sizeof p, t, ".gtkrc-2.0");
	if (on(t, "gtk.gtk2") && gtk2_name(t, p) < 0)
		return -1;
	if (on(t, "gtk.gtk2") &&
	    tm_blockf(p, "gtk", "#", "", 0,
	              "gtk-icon-theme-name=\"%s\"\ngtk-cursor-theme-name=\"%s\"\n"
	              "gtk-cursor-theme-size=%d\ngtk-font-name=\"%s\"\n"
	              "gtk-xft-antialias=%d\ngtk-xft-hinting=%d\ngtk-xft-hintstyle=\"%s\"\n"
	              "gtk-xft-rgba=\"%s\"\n",
	              t->icons, t->cursor, t->cursor_size, font, t->antialias ? 1 : 0,
	              strcmp(t->hinting, "none") ? 1 : 0, hintstyle(t), t->rgba) < 0)
		return -1;

	if (!xs || strcmp(xs, "off")) {
		tm_path(p, sizeof p, t, ".config/xsettingsd/xsettingsd.conf");
		if (tm_blockf(p, "gtk", "#", "", 0,
		              "Net/ThemeName \"%s\"\nNet/IconThemeName \"%s\"\n"
		              "Gtk/CursorThemeName \"%s\"\nGtk/CursorThemeSize %d\n"
		              "Gtk/FontName \"%s\"\nXft/Antialias %d\nXft/Hinting %d\n"
		              "Xft/HintStyle \"%s\"\nXft/RGBA \"%s\"\n",
		              theme_name(t), t->icons, t->cursor, t->cursor_size, font, t->antialias ? 1 : 0,
		              strcmp(t->hinting, "none") ? 1 : 0, hintstyle(t), t->rgba) < 0)
			return -1;
	}

	/* without a session bus gsettings keeps its writes in memory and drops them:
	 * nothing to do, and ten programs not started */
	if ((!gs || strcmp(gs, "off")) && tm_have("gsettings") && session_bus()) {
		gsettings_save();
		gsettings(t, font);
	}

	/* No GTK_THEME: the variable overrides settings.ini, and a session that still
	 * carries an old value (a renamed set) gets Adwaita for everything. The theme
	 * name reaches GTK through settings.ini, gsettings, and xsettingsd. */
	return 0;
}

/* xsettingsd re-reads its file on SIGHUP. The GTK file-chooser portal (what
 * Firefox and sandboxed programs open their file dialogs through on Wayland) is a
 * long-running service that reads the theme once: restart it when it runs, so the
 * dialogs follow the theme without a logout. Nothing else needs telling. */
static void
reload(void)
{
	static const char *const portal[] = { "systemctl", "--user", "try-restart",
	                                      "xdg-desktop-portal-gtk.service", NULL };

	tm_signal("xsettingsd", SIGHUP);
	if (tm_have("systemctl") && session_bus())
		tm_run(portal);
}

/* puts the gsettings keys back as they were before themer (gsettings_save); the
 * file goes once they are back, so it is kept while there is no session bus */
static void
unload(void)
{
	char p[PATH_MAX], line[640];
	FILE *f;
	int ok = 1;

	saved_path(p, sizeof p);
	if (!(f = fopen(p, "r")))
		return;
	if (!tm_have("gsettings") || !session_bus()) {
		fclose(f);
		return;
	}
	while (fgets(line, sizeof line, f)) {
		char *tab = strchr(line, '\t');
		const char *argv[] = { "gsettings", "set", "org.gnome.desktop.interface", line, NULL, NULL };

		line[strcspn(line, "\n")] = '\0';
		if (!tab)
			continue;
		*tab = '\0';
		argv[4] = tab + 1;
		if (tm_run(argv) != 0)
			ok = 0;
	}
	fclose(f);
	if (ok)
		unlink(p);
}

/* any of GTK 2, 3 and 4 */
static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have_lib("libgtk-3.so.0") || tm_have_lib("libgtk-4.so.1") || tm_have_lib("libgtk-x11-2.0.so.0");
}

static const TmModule mod = {
	.name = "gtk",
	.desc = "GTK 2, 3 and 4: theme, icons, cursor, font and rendering",
	.templates = "gtk",
	.default_on = 1,
	.present = present,
	.load = load,
	.unload = unload,
	.reload = reload,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
