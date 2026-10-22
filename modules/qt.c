/* qt.c: Qt 5 and Qt 6, following the style contract in themer_module.h. Same
 * roles as the GTK, Firefox, and sway modules. One stylesheet,
 * templates/<set>/qt/qt.qss, and one palette, colors.conf, serve both; anything
 * else in the set's qt/ directory (images the stylesheet refers to) is copied
 * beside the stylesheet.
 *
 * Qt takes a palette, fonts, and an icon theme only from a platform theme plugin,
 * so themer ships one (gui/qpa/themerqpa.cpp, built for Qt 5 and Qt 6, installed
 * under PREFIX/lib/themer/qt{5,6}/platformthemes). This module writes what it
 * reads, ~/.config/themer/qt/theme.conf and themer.qss, and sets
 * QT_QPA_PLATFORMTHEME=themer and QT_PLUGIN_PATH in the environment file. The
 * plugin watches those files, so running Qt programs restyle on apply. Where
 * qt5ct or qt6ct are installed their configs are written too, and the
 * platformtheme option can select them instead (they were the only way before).
 *
 * The palette maps each role onto the contract. Fusion draws its bevels from
 * Light and Dark, which here are the surface color and the outline color, so
 * controls get the same flat, outlined look as the GTK theme. A flat
 * stylesheet adds the menus, tabs, buttons, and fields Fusion draws its own
 * way. Fonts come from the theme's UI and terminal fonts.
 *
 * What the theme implies is written: the style, the palette, the stylesheet, the
 * icon theme, and the fonts. The Interface page of qt5ct (dialogs, double-click
 * interval, wheel lines, icons in menus...) is the user's own and is left alone.
 *
 * With platformtheme=qt5ct the environment file says qt5ct: that name is answered
 * by qt5ct for Qt 5 programs and by qt6ct for Qt 6 ones (its plugin registers both
 * keys), so one value themes both. */
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "themer_module.h"

#define ROLE_COUNT 21

/* One color set, in QPalette::ColorRole order:
 * WindowText, Button, Light, Midlight, Dark, Mid, Text, BrightText,
 * ButtonText, Base, Window, Shadow, Highlight, HighlightedText, Link,
 * LinkVisited, AlternateBase, NoRole, ToolTipBase, ToolTipText, PlaceholderText */
static void
role_list(char *o, size_t n, const char *const r[ROLE_COUNT])
{
	char tmp[32];

	o[0] = '\0';
	for (int i = 0; i < ROLE_COUNT; i++) {
		snprintf(tmp, sizeof tmp, "%s#%s", i ? ", " : "", r[i]);
		strncat(o, tmp, n - strlen(o) - 1);
	}
}

/* disabled text: the background moved toward the text until it reads at 4.5:1 */
static void
disabled_of(char *out, size_t n, const TmTheme *t)
{
	tm_ensure_contrast(out, n, t->bg, t->bg, t->fg, 4.5);
}

static void
palette(char *active, char *disabled, char *inactive, size_t n, const TmTheme *t)
{
	char dis[TM_COLOR_LEN], hb[TM_COLOR_LEN], hf[TM_COLOR_LEN];
	/* selection in widgets the stylesheet does not cover uses the hover colors, as GTK does */
	if (tm_shared(t, "hover_bg", hb, sizeof hb) < 0 || tm_shared(t, "hover_fg", hf, sizeof hf) < 0) {
		snprintf(hb, sizeof hb, "%s", t->acc);
		snprintf(hf, sizeof hf, "%s", t->accfg);
	}
	const char *act[ROLE_COUNT] = {
		t->fg, t->surf, t->surf, t->surf, t->line, t->line, t->fg, t->accfg,
		t->fg, t->surf, t->bg, t->line, hb, hf, t->acc,
		t->acc, t->bg, t->bg, t->bg, t->fg, t->ansi[8],
	};
	const char *ina[ROLE_COUNT] = {
		t->fg, t->surf, t->surf, t->surf, t->line, t->line, t->fg, t->fg,
		t->fg, t->surf, t->bg, t->line, t->surf, t->fg, t->fg,
		t->fg, t->bg, t->bg, t->bg, t->fg, t->ansi[8],
	};
	disabled_of(dis, sizeof dis, t);
	{
		const char *dsb[ROLE_COUNT] = {
			dis, t->surf, t->surf, t->surf, t->line, t->line, dis, dis,
			dis, t->surf, t->bg, t->line, t->surf, dis, dis,
			dis, t->bg, t->bg, t->bg, dis, t->ansi[8],
		};
		role_list(active, n, act);
		role_list(disabled, n, dsb);
	}
	role_list(inactive, n, ina);
}

/* the module's own variables: the three palette role lists, for colors.conf, and
 * qt_assets, the directory the stylesheet's images are written to (themer's own
 * here; qt_sheet gives the real one for each copy) */
static int
qt_vars(const TmTheme *t, TmVar *v, int max)
{
	static char active[1024], disabled[1024], inactive[1024], assets[PATH_MAX];

	if (max < 4)
		return 0;
	palette(active, disabled, inactive, sizeof active, t);
	tm_path(assets, sizeof assets, t, ".config/themer/qt");
	v[0] = (TmVar){ "qt_active", active };
	v[1] = (TmVar){ "qt_disabled", disabled };
	v[2] = (TmVar){ "qt_inactive", inactive };
	v[3] = (TmVar){ "qt_assets", assets };
	return 4;
}

TM_VARS(qt_vars)

static const TmOption opts[] = {
	{ "font", "font for Qt widgets (the fixed font is always the terminal font)", "ui|terminal", "ui", 0, 0 },
	{ "qt5", "theme Qt 5 programs", "on|off", "on", 0, 0 },
	{ "qt6", "theme Qt 6 programs", "on|off", "on", 0, 0 },
	{ "platformtheme", "how Qt programs take the theme: themer's own plugin, or qt5ct/qt6ct; auto picks the plugin when it is installed", "auto|themer|qt5ct", "auto", 0, 0 },
	TM_OPT_TEMPLATE,
};

/* The stylesheet and its images into dir: qt.qss rendered as dir/themer.qss with
 * {{qt_assets}} naming dir (a stylesheet's url() is relative to the program's working
 * directory, not to the stylesheet, so the paths must be absolute); every top-level
 * .svg of the set's qt/ rendered too, so the arrows and marks take the theme's
 * colors; everything else in the set's qt/ copied as it is. */
static int
qt_sheet(const TmTheme *t, const char *dir)
{
	static char sheet[256 * 1024], img[65536];
	static const char *const skip[] = { "qt.qss", "colors.conf", "set.conf", NULL };
	char p[PATH_MAX], setdir[PATH_MAX], files[64][64];
	const char *set = tm_opt(t, "qt.template");
	const char *skipall[64 + 4];
	TmVar v[1] = { { "qt_assets", dir } };
	int nf = 0, ns = 0;

	if (tm_render(sheet, sizeof sheet, "qt", set, "qt.qss", t, v, 1) < 0)
		return -1;
	if (snprintf(p, sizeof p, "%.*s/themer.qss", (int)sizeof p - 16, dir) >= (int)sizeof p ||
	    tm_writef(p, "%s", sheet) < 0)
		return -1;
	/* the images: the set's, or the default set's when it has none */
	if (tm_template_dir("qt", set, setdir, sizeof setdir) < 0 &&
	    tm_template_dir("qt", NULL, setdir, sizeof setdir) < 0)
		return 0;
	{
		DIR *d = opendir(setdir);
		struct dirent *e;

		while (d && (e = readdir(d)) && nf < 64) {
			size_t len = strlen(e->d_name);

			if (len > 4 && len < 64 && !strcmp(e->d_name + len - 4, ".svg"))
				snprintf(files[nf++], 64, "%.63s", e->d_name);
		}
		if (d)
			closedir(d);
	}
	for (int i = 0; i < nf; i++) {
		size_t len;

		if (tm_render(img, sizeof img, "qt", set, files[i], t, v, 1) < 0)
			return -1;
		len = strlen(img);
		if (snprintf(p, sizeof p, "%.*s/%s", (int)sizeof p - 80, dir, files[i]) >= (int)sizeof p ||
		    tm_write_asset(p, img, len) < 0)
			return -1;
	}
	for (int i = 0; i < 3; i++)
		skipall[ns++] = skip[i];
	for (int i = 0; i < nf; i++)
		skipall[ns++] = files[i];
	skipall[ns] = NULL;
	return tm_copy_tree(setdir, dir, skipall) < 0 ? -1 : 0;
}

static int
qt_one(const TmTheme *t, const char *ct)
{
	char p[PATH_MAX], colors[PATH_MAX], qss[PATH_MAX], cpath[PATH_MAX], qpath[PATH_MAX], dir[PATH_MAX];
	char scheme[2048];
	char font[256], fixed[256], fam[128];
	const char *set = tm_opt(t, "qt.template");
	TmVar v[4];
	int size, nv = qt_vars(t, v, TM_COUNT(v));

	snprintf(colors, sizeof colors, ".config/%s/colors/themer.conf", ct);
	/* the stylesheet in a directory of its own, so a set's images sit beside it */
	snprintf(qss, sizeof qss, ".config/%s/qss/themer/themer.qss", ct);
	if (tm_render(scheme, sizeof scheme, "qt", set, "colors.conf", t, v, nv) < 0)
		return -1;
	tm_path(p, sizeof p, t, colors);
	if (tm_writef(p, "%s", scheme) < 0)
		return -1;
	snprintf(p, sizeof p, ".config/%s/qss/themer", ct);
	tm_path(dir, sizeof dir, t, p);
	if (qt_sheet(t, dir) < 0)
		return -1;

	tm_font_for(t, "qt.font", fam, sizeof fam, &size);
	snprintf(font, sizeof font, "\"%s,%d,-1,5,50,0,0,0,0,0\"", fam, size);
	snprintf(fixed, sizeof fixed, "\"%s,%d,-1,5,50,0,0,0,0,0\"", t->mono_font, t->mono_size);

	/* the user's own qtNct.conf is edited key by key */
	if (snprintf(cpath, sizeof cpath, "%s/%s", t->home, colors) >= (int)sizeof cpath ||
	    snprintf(qpath, sizeof qpath, "%s/%s", t->home, qss) >= (int)sizeof qpath)
		return -1;
	snprintf(p, sizeof p, "%s/.config/%s/%s.conf", t->home, ct, ct);
	tm_ini(p, "Appearance", "style", "Fusion");
	tm_ini(p, "Appearance", "custom_palette", "true");
	tm_ini(p, "Appearance", "color_scheme_path", cpath);
	tm_ini(p, "Appearance", "icon_theme", t->icons);
	tm_ini(p, "Appearance", "stylesheets", qpath);
	tm_ini(p, "Fonts", "general", font);
	tm_ini(p, "Fonts", "fixed", fixed);
	return 0;
}

/* Qt itself (its libraries, or its tools), or qt5ct/qt6ct */
static int
present(const TmTheme *t)
{
	static const char *const libs[] = {
		"/usr/lib/libQt6Core.so.6", "/usr/lib/libQt5Core.so.5",
		"/usr/lib64/libQt6Core.so.6", "/usr/lib64/libQt5Core.so.5",
		"/usr/lib/x86_64-linux-gnu/libQt6Core.so.6", "/usr/lib/x86_64-linux-gnu/libQt5Core.so.5",
		"/usr/lib/aarch64-linux-gnu/libQt6Core.so.6", "/usr/lib/aarch64-linux-gnu/libQt5Core.so.5",
	};
	char p5[PATH_MAX], p6[PATH_MAX];

	for (size_t i = 0; i < TM_COUNT(libs); i++)
		if (!access(libs[i], F_OK))
			return 1;
	tm_path(p5, sizeof p5, t, ".config/qt5ct");
	tm_path(p6, sizeof p6, t, ".config/qt6ct");
	return tm_have("qtpaths") || tm_have("qtpaths6") || tm_have("qmake") || tm_have("qmake6") ||
	       tm_have("qt5ct") || tm_have("qt6ct") || !access(p5, F_OK) || !access(p6, F_OK);
}

#ifndef SYS_QT_PLUGINS_DIR
#define SYS_QT_PLUGINS_DIR "/usr/local/lib/themer/qt"
#endif

/* the directory holding qt5/ and qt6/ with themer's platform theme plugin:
 * $THEMER_QT_PLUGINS (a build tree), else where make install put it */
static const char *
plugins_dir(void)
{
	const char *env = getenv("THEMER_QT_PLUGINS");

	return env && *env ? env : SYS_QT_PLUGINS_DIR;
}

/* an on|off option, on unless set to off */
static int
on(const TmTheme *t, const char *key)
{
	const char *v = tm_opt(t, key);

	return !v || strcmp(v, "off");
}

/* 1 if the plugin is installed for a Qt major that is on */
static int
plugin_installed(const TmTheme *t)
{
	char p[PATH_MAX];

	for (int v = 5; v <= 6; v++) {
		if (!on(t, v == 5 ? "qt.qt5" : "qt.qt6"))
			continue;
		snprintf(p, sizeof p, "%s/qt%d/platformthemes/libthemer.so", plugins_dir(), v);
		if (!access(p, R_OK))
			return 1;
	}
	return 0;
}

/* themer's own files: theme.conf for the plugin, the stylesheet, the set's assets */
static int
qt_themer(const TmTheme *t)
{
	char p[PATH_MAX], dir[PATH_MAX], fam[128];
	char active[1024], disabled[1024], inactive[1024];
	int size;

	tm_path(dir, sizeof dir, t, ".config/themer/qt");
	if (qt_sheet(t, dir) < 0)
		return -1;
	palette(active, disabled, inactive, sizeof active, t);
	tm_font_for(t, "qt.font", fam, sizeof fam, &size);
	if (snprintf(p, sizeof p, "%.*s/theme.conf", (int)sizeof p - 16, dir) >= (int)sizeof p)
		return -1;
	return tm_writef(p, "# generated by themer: read by themer's Qt platform theme plugin\n"
	                    "[themer]\nstyle=Fusion\nicon_theme=%s\nfont=%s\nfont_size=%d\n"
	                    "fixed_font=%s\nfixed_size=%d\nstylesheet=%s/themer.qss\n"
	                    "hinting=%s\nantialias=%d\n"
	                    "active=%s\ndisabled=%s\ninactive=%s\n",
	                 t->icons, fam, size, t->mono_font, t->mono_size, dir, t->hinting, t->antialias ? 1 : 0,
	                 active, disabled, inactive);
}

static int
load(const TmTheme *t)
{
	char p[PATH_MAX], p5[PATH_MAX], p6[PATH_MAX];
	const char *mode = tm_opt(t, "qt.platformtheme");
	int use_plugin = !mode || !strcmp(mode, "auto") ? plugin_installed(t) : !strcmp(mode, "themer");
	int q5 = on(t, "qt.qt5"), q6 = on(t, "qt.qt6");

	if (!q5 && !q6)
		return 0;
	if (qt_themer(t) < 0)
		return -1;
	/* qt5ct and qt6ct, where they are: their users keep working, and they are the
	 * fallback where the plugin is not installed */
	tm_path(p5, sizeof p5, t, ".config/qt5ct");
	tm_path(p6, sizeof p6, t, ".config/qt6ct");
	if (q5 && (tm_have("qt5ct") || !access(p5, F_OK)) && qt_one(t, "qt5ct") < 0)
		return -1;
	if (q6 && (tm_have("qt6ct") || !access(p6, F_OK)) && qt_one(t, "qt6ct") < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/environment.d/40-themer-qt.conf");
	if (use_plugin) {
		char path[2 * PATH_MAX + 2];

		snprintf(path, sizeof path, "%s%s%s%s", q5 ? plugins_dir() : "", q5 ? "/qt5" : "",
		         q5 && q6 ? ":" : "", q6 ? plugins_dir() : "");
		if (q6)
			strncat(path, "/qt6", sizeof path - strlen(path) - 1);
		return tm_writef(p, "# generated by themer\nQT_QPA_PLATFORMTHEME=themer\nQT_PLUGIN_PATH=%s\n", path);
	}
	if (!plugin_installed(t) && !tm_have("qt5ct") && !tm_have("qt6ct"))
		fprintf(stderr, "themer: qt: neither themer's Qt plugin (%s) nor qt5ct/qt6ct is installed; "
		        "Qt programs will stay plain\n", plugins_dir());
	return tm_writef(p, "# generated by themer\nQT_QPA_PLATFORMTHEME=qt5ct\n");
}

static const TmModule mod = {
	.name = "qt",
	.desc = "Qt 5 and 6: palette, flat stylesheet, icons, fonts, through themer's own platform theme",
	.templates = "qt",
	.default_on = 1,
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
