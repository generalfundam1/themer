/* kde.c: KDE's colors and fonts, for KDE and Qt programs (Dolphin, Kate, Okular,
 * Konsole's window, KWin's title bars) that read their palette from kdeglobals.
 *
 * Writes:
 *   ~/.local/share/color-schemes/Themer.colors   the theme as a KDE color scheme,
 *                                                so System Settings lists it
 *   ~/.config/kdeglobals                         the same keys set in place (tm_ini),
 *                                                with [General] ColorScheme=Themer,
 *                                                and the fonts
 * KDE programs read kdeglobals at start and pick up a change while running when
 * Plasma tells them; outside Plasma, restart them. Unload puts every key back.
 *
 * The scheme is KDE's own layout (Breeze's sets: Window, View, Button, Selection,
 * Tooltip, Complementary, Header, and the title bars under [WM]), recolored after
 * the style contract: views are the Adwaita view tone, headers and buttons the raised
 * tone, selection the accent, negative/neutral/positive text the urgent color, the
 * warning color and ansi2.
 *
 * Options (after the host's mode):
 *   font   ui (the default) or terminal: the font for menus, toolbars and windows;
 *          the fixed font is always the terminal font */
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "themer_module.h"

static const TmOption opts[] = {
	{ "font", "font for KDE's menus, toolbars and windows (the fixed font is always the terminal font)", "ui|terminal", "ui", 0, 0 },
};

/* KDE Frameworks, which every KDE program links against */
static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have_lib("libKF6ConfigCore.so.6") || tm_have_lib("libKF5ConfigCore.so.5") ||
	       tm_have("kreadconfig6") || tm_have("kreadconfig5");
}

/* "r,g,b" from six hex digits, as KDE writes colors */
static const char *
rgb(const char *hex, char *out, size_t n)
{
	unsigned r = 0, g = 0, b = 0;

	sscanf(hex, "%2x%2x%2x", &r, &g, &b);
	snprintf(out, n, "%u,%u,%u", r, g, b);
	return out;
}

typedef struct { const char *section, *key; char value[32]; } Key;

#define MAX_KEYS 160
static Key keys[MAX_KEYS];
static int nkeys;

static void
put(const char *section, const char *key, const char *hex)
{
	if (nkeys < MAX_KEYS) {
		keys[nkeys].section = section;
		keys[nkeys].key = key;
		rgb(hex, keys[nkeys].value, sizeof keys[nkeys].value);
		nkeys++;
	}
}

/* one color set: its backgrounds and the text over them */
static void
colorset(const char *section, const char *bg, const char *alt, const char *fg, const char *inactive,
         const TmTheme *t, const char *link, const char *visited, const char *positive, const char *focus,
         const char *negative, const char *neutral)
{
	put(section, "BackgroundNormal", bg);
	put(section, "BackgroundAlternate", alt);
	put(section, "ForegroundNormal", fg);
	put(section, "ForegroundInactive", inactive);
	put(section, "ForegroundActive", focus);
	put(section, "ForegroundLink", link);
	put(section, "ForegroundVisited", visited);
	put(section, "ForegroundNegative", negative ? negative : t->urg);
	put(section, "ForegroundNeutral", neutral ? neutral : t->warn);
	put(section, "ForegroundPositive", positive);
	put(section, "DecorationFocus", t->acc);
	put(section, "DecorationHover", t->acc);
}

static int
load(const TmTheme *t)
{
	char view[TM_COLOR_LEN], raised[TM_COLOR_LEN], control[TM_COLOR_LEN], sidebar[TM_COLOR_LEN];
	char dis[TM_COLOR_LEN], link[TM_COLOR_LEN], visited[TM_COLOR_LEN], pos[TM_COLOR_LEN], focus[TM_COLOR_LEN];
	char scheme[PATH_MAX], globals[PATH_MAX], body[16384], font[160], fixed[160], small[160], fam[128];
	char bg_rgb[16], sel_neg[TM_COLOR_LEN], sel_neu[TM_COLOR_LEN], sel_pos[TM_COLOR_LEN];
	const char *cur = NULL;
	size_t o = 0;
	int size;

	if (tm_shared(t, "view", view, sizeof view) < 0 || tm_shared(t, "raised", raised, sizeof raised) < 0 ||
	    tm_shared(t, "control", control, sizeof control) < 0 || tm_shared(t, "sidebar", sidebar, sizeof sidebar) < 0 ||
	    tm_shared(t, "disabled", dis, sizeof dis) < 0)
		return -1;
	/* text colors that read on every set's background (the view is the lightest or
	 * darkest of them, the raised tone the other end) */
	tm_ensure_contrast(link, sizeof link, t->ansi[4], view, t->fg, 4.5);
	tm_ensure_contrast(visited, sizeof visited, t->ansi[5], view, t->fg, 4.5);
	tm_ensure_contrast(pos, sizeof pos, t->ansi[2], view, t->fg, 4.5);
	tm_ensure_contrast(focus, sizeof focus, t->acc, view, t->fg, 3.0);

	/* on the selection, the state colors are moved until they read on the accent (the
	 * window's own red or yellow on a selected row was often under 2:1) */
	tm_ensure_contrast(sel_neg, sizeof sel_neg, t->urg, t->acc, t->accfg, 4.5);
	tm_ensure_contrast(sel_neu, sizeof sel_neu, t->warn, t->acc, t->accfg, 4.5);
	tm_ensure_contrast(sel_pos, sizeof sel_pos, t->ansi[2], t->acc, t->accfg, 4.5);

	nkeys = 0;
	colorset("Colors:Window", t->bg, sidebar, t->fg, dis, t, link, visited, pos, focus, NULL, NULL);
	colorset("Colors:View", view, raised, t->fg, dis, t, link, visited, pos, focus, NULL, NULL);
	colorset("Colors:Button", raised, control, t->fg, dis, t, link, visited, pos, focus, NULL, NULL);
	colorset("Colors:Selection", t->acc, t->acc, t->accfg, t->accfg, t, t->accfg, t->accfg, sel_pos, t->accfg, sel_neg, sel_neu);
	colorset("Colors:Tooltip", raised, raised, t->fg, dis, t, link, visited, pos, focus, NULL, NULL);
	colorset("Colors:Complementary", t->bg, sidebar, t->fg, dis, t, link, visited, pos, focus, NULL, NULL);
	colorset("Colors:Header", raised, raised, t->fg, dis, t, link, visited, pos, focus, NULL, NULL);
	colorset("Colors:Header][Inactive", t->bg, t->bg, t->fg, dis, t, link, visited, pos, focus, NULL, NULL);
	put("WM", "activeBackground", raised);
	put("WM", "activeForeground", t->fg);
	put("WM", "activeBlend", raised);
	put("WM", "inactiveBackground", t->bg);
	put("WM", "inactiveForeground", dis);
	put("WM", "inactiveBlend", t->bg);

	tm_font_for(t, "kde.font", fam, sizeof fam, &size);
	snprintf(font, sizeof font, "%s,%d", fam, size);
	snprintf(fixed, sizeof fixed, "%s,%d", t->mono_font, t->mono_size);
	snprintf(small, sizeof small, "%s,%d", fam, size > 9 ? size - 2 : size);

	/* the scheme file, section by section */
	o += snprintf(body + o, sizeof body - o,
	              "# generated by themer (%s, %s): its colors as a KDE color scheme\n"
	              "[General]\nName=Themer\nColorScheme=Themer\n\n"
	              "[KDE]\ncontrast=4\n\n"
	              "[ColorEffects:Disabled]\nColor=%s\nColorAmount=0\nColorEffect=0\nContrastAmount=0.65\n"
	              "ContrastEffect=1\nIntensityAmount=0.1\nIntensityEffect=2\n\n"
	              "[ColorEffects:Inactive]\nChangeSelectionColor=false\nEnable=false\n",
	              t->theme, t->dark ? "dark" : "light", rgb(t->bg, bg_rgb, sizeof bg_rgb));
	for (int i = 0; i < nkeys && o < sizeof body; i++) {
		if (!cur || strcmp(cur, keys[i].section))
			o += snprintf(body + o, sizeof body - o, "\n[%s]\n", cur = keys[i].section);
		o += snprintf(body + o, sizeof body - o, "%s=%s\n", keys[i].key, keys[i].value);
	}
	tm_path(scheme, sizeof scheme, t, ".local/share/color-schemes/Themer.colors");
	if (tm_writef(scheme, "%s", body) < 0)
		return -1;

	/* and the same keys in kdeglobals, which the programs read */
	tm_path(globals, sizeof globals, t, ".config/kdeglobals");
	for (int i = 0; i < nkeys; i++)
		if (tm_ini(globals, keys[i].section, keys[i].key, keys[i].value) < 0)
			return -1;
	/* the scheme's effects and contrast too, as System Settings copies them when it
	 * applies a scheme: without them KDE programs kept the inactive-window effect of the
	 * scheme before (a darkened selection) that Themer.colors turns off */
	{
		static const char *const fx[][3] = {
			{ "ColorEffects:Disabled", "ColorAmount", "0" }, { "ColorEffects:Disabled", "ColorEffect", "0" },
			{ "ColorEffects:Disabled", "ContrastAmount", "0.65" }, { "ColorEffects:Disabled", "ContrastEffect", "1" },
			{ "ColorEffects:Disabled", "IntensityAmount", "0.1" }, { "ColorEffects:Disabled", "IntensityEffect", "2" },
			{ "ColorEffects:Inactive", "ChangeSelectionColor", "false" }, { "ColorEffects:Inactive", "Enable", "false" },
			{ "KDE", "contrast", "4" },
		};

		tm_ini(globals, "ColorEffects:Disabled", "Color", bg_rgb);
		for (size_t i = 0; i < sizeof fx / sizeof fx[0]; i++)
			tm_ini(globals, fx[i][0], fx[i][1], fx[i][2]);
	}
	tm_ini(globals, "General", "ColorScheme", "Themer");
	tm_ini(globals, "General", "font", font);
	tm_ini(globals, "General", "menuFont", font);
	tm_ini(globals, "General", "toolBarFont", font);
	tm_ini(globals, "General", "smallestReadableFont", small);
	tm_ini(globals, "General", "fixed", fixed);
	tm_ini(globals, "WM", "activeFont", font);
	tm_ini(globals, "Icons", "Theme", t->icons);
	return 0;
}

static const TmModule mod = {
	.name = "kde",
	.desc = "KDE: the colors and fonts KDE programs read (kdeglobals, a Themer color scheme)",
	.default_on = 1,
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
