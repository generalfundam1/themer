/* gtk.c: GTK 2, 3, and 4, and the settings nwg-look covers.
 *
 * The set's gtk/ directory becomes a GTK theme, written to ~/.config/themer/gtk/ like
 * the qt module's files; ~/.themes/<set> is a link to it, so GTK finds it under the
 * set's name (themer_chicago, labelled "Themer Chicago" from set.conf): every top-level
 * .css file is rendered into gtk-3.0/ and gtk-4.0/ (gtk.css serves both; a
 * gtk4.css, if the set has one, is GTK 4's gtk.css instead; a gtk-dark.css is what
 * GTK 3 loads in dark mode, as in any theme), and everything else in the directory
 * (assets/, images, fonts) is copied beside them as it is, so a theme converted
 * from an existing one keeps its url() references. GTK 3 selects the theme through
 * settings.ini (gtk-theme-name), never GTK_THEME: that variable overrides the file
 * and goes stale when the set changes. GTK 4 ignores ~/.themes, so a
 * marked block at the top of ~/.config/gtk-4.0/gtk.css imports the theme's file.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* the GTK theme's name: the set's, so switching sets switches themes */
static const char *
theme_name(const TmTheme *t)
{
	const char *set = tm_opt(t, "gtk.template");

	return set && *set ? set : TM_DEFAULT_SET;
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

/* one gsettings key; output is dropped since there may be no session bus */
static void
gset(const char *key, const char *fmt, const char *value)
{
	char cmd[1024];

	if (snprintf(cmd, sizeof cmd, fmt, key, value) < (int)sizeof cmd)
		if (system(cmd) != 0)
			return;
}

static void
gsettings(const TmTheme *t, const char *font)
{
	char mono[160], num[16];
	const char *aa = !t->antialias ? "none" : strcmp(t->rgba, "none") ? "rgba" : "grayscale";
	const char *str = "gsettings set org.gnome.desktop.interface %s \"'%s'\" >/dev/null 2>&1";
	const char *raw = "gsettings set org.gnome.desktop.interface %s %s >/dev/null 2>&1";

	snprintf(mono, sizeof mono, "%s %d", t->mono_font, t->mono_size);
	snprintf(num, sizeof num, "%d", t->cursor_size);
	gset("gtk-theme", str, theme_name(t));
	gset("icon-theme", str, t->icons);
	gset("cursor-theme", str, t->cursor);
	gset("cursor-size", raw, num);
	gset("font-name", str, font);
	gset("monospace-font-name", str, mono);
	gset("color-scheme", str, t->dark ? "prefer-dark" : "default");
	gset("font-antialiasing", str, aa);
	gset("font-hinting", str, t->hinting);
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

/* The theme directories: every top-level .css of the set's gtk/ rendered into
 * gtk-3.0/ and gtk-4.0/, the rest of the directory copied into both. */
static int
theme_dirs(const TmTheme *t)
{
	static char css[256 * 1024];
	static const char *const versions[] = { "gtk-3.0", "gtk-4.0" };
	const char *set = tm_opt(t, "gtk.template");
	char dir[PATH_MAX], p[PATH_MAX], dst[PATH_MAX];
	const char *skip[64];
	int nskip = 0;
	DIR *d;
	struct dirent *e;

	if (tm_template_dir("gtk", set, dir, sizeof dir) < 0) {
		/* a set without a gtk directory: the default set's, file by file */
		if (tm_template_dir("gtk", NULL, dir, sizeof dir) < 0)
			return -1;
	}
	if (!(d = opendir(dir)))
		return -1;
	while ((e = readdir(d)) && nskip < 63) {
		size_t len = strlen(e->d_name);

		if (e->d_name[0] == '.' || len < 5 || strcmp(e->d_name + len - 4, ".css"))
			continue;
		skip[nskip++] = strdup(e->d_name);
	}
	closedir(d);
	skip[nskip] = NULL;
	for (int i = 0; i < nskip; i++) {
		const char *name = skip[i];
		int gtk4_only = !strcmp(name, "gtk4.css");

		if (tm_render(css, sizeof css, "gtk", set, name, t, NULL, 0) < 0)
			return -1;
		for (int v = 0; v < 2; v++) {
			const char *out = name;

			if (!on(t, v ? "gtk.gtk4" : "gtk.gtk3"))
				continue;
			if (gtk4_only) {
				if (v == 0)
					continue;
				out = "gtk.css";
			} else if (v == 1 && !strcmp(name, "gtk.css")) {
				/* gtk4.css, when the set has one, is GTK 4's gtk.css */
				int has4 = 0;

				for (int k = 0; k < nskip; k++)
					if (!strcmp(skip[k], "gtk4.css"))
						has4 = 1;
				if (has4)
					continue;
			}
			snprintf(p, sizeof p, ".config/themer/gtk/%s/%s", versions[v], out);
			tm_path(dst, sizeof dst, t, p);
			if (tm_writef(dst, "%s", css) < 0)
				return -1;
		}
	}
	for (int v = 0; v < 2; v++) {
		if (!on(t, v ? "gtk.gtk4" : "gtk.gtk3"))
			continue;
		snprintf(p, sizeof p, ".config/themer/gtk/%s", versions[v]);
		tm_path(dst, sizeof dst, t, p);
		if (tm_copy_tree(dir, dst, skip) < 0)
			return -1;
	}
	for (int i = 0; i < nskip; i++)
		free((char *)skip[i]);
	return 0;
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
		char rel[PATH_MAX], label[128];

		/* the label: set.conf's name, else the set's name */
		if (tm_set_info(theme_name(t), "name", label, sizeof label) < 0 || !label[0])
			snprintf(label, sizeof label, "%s", theme_name(t));
		tm_path(p, sizeof p, t, ".config/themer/gtk/index.theme");
		if (tm_writef(p, "# generated by themer\n[Desktop Entry]\nType=X-GNOME-Metatheme\n"
		                 "Name=%s\nComment=a themer template set\n[X-GNOME-Metatheme]\n"
		                 "GtkTheme=%s\nIconTheme=%s\nCursorTheme=%s\n",
		              label, theme_name(t), t->icons, t->cursor) < 0)
			return -1;
		/* where GTK looks for themes: a link under the set's name to themer's files */
		snprintf(rel, sizeof rel, ".themes/%s", theme_name(t));
		tm_path(p, sizeof p, t, rel);
		{
			char target[PATH_MAX];

			tm_path(target, sizeof target, t, ".config/themer/gtk");
			if (tm_symlink(target, p) < 0)
				return -1;
		}
	}
	retire_old_css(t);
	tm_path(p, sizeof p, t, ".config/gtk-4.0/gtk.css");
	if (on(t, "gtk.gtk4") &&
	    tm_blockf(p, "gtk", "/*", " */", 1,
	              "@import url(\"file://%s/.config/themer/gtk/gtk-4.0/gtk.css\");\n", t->home) < 0)
		return -1;

	tm_font_for(t, "gtk.font", fam, sizeof fam, &size);
	if (snprintf(font, sizeof font, "%s %d", fam, size) >= (int)sizeof font)
		return -1;

	if (on(t, "gtk.gtk3"))
		settings_ini(t, ".config/gtk-3.0/settings.ini", font, 1);
	if (on(t, "gtk.gtk4"))
		settings_ini(t, ".config/gtk-4.0/settings.ini", font, 0);

	/* GTK 2 has no themer theme; it gets the icons, cursor, font, and rendering */
	tm_path(p, sizeof p, t, ".gtkrc-2.0");
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

	if ((!gs || strcmp(gs, "off")) && tm_have("gsettings"))
		gsettings(t, font);

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
	if (tm_have("pkill"))
		if (system("pkill -HUP -x xsettingsd >/dev/null 2>&1") != 0)
			return;
	if (tm_have("systemctl") && getenv("DBUS_SESSION_BUS_ADDRESS"))
		if (system("systemctl --user try-restart xdg-desktop-portal-gtk.service >/dev/null 2>&1") != 0)
			return;
}

static const TmModule mod = {
	.name = "gtk",
	.desc = "GTK 2/3/4: theme, icons, cursor, font, rendering; settings.ini, xsettingsd, gsettings",
	.templates = "gtk",
	.default_on = 1,
	.load = load,
	.reload = reload,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
