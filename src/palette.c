/* palette.c: the shared template variables. Every template, whichever module
 * renders it, sees the same names: the theme's roles, the Chicago and Adwaita tones
 * derived from them, the fonts, the terminal colors, and the padding scale. The derived
 * tones are what make GTK, Qt, Firefox, and sway look like one design.
 *
 * Chicago tones, each from the theme's roles. The bevel is light on dark themes
 * and dark on light ones.
 *   face          control surface (surf)
 *   light, shade  raised controls: lit top and left, shaded bottom and right;
 *                 sunken fields the other way round
 *   pressed       a control held down or checked
 *   field         text fields and lists (bg)
 *   hover_bg      hover and selection: the accent, or the background moved toward
 *                 the text until it stands out 3:1
 *   hover_fg      text on hover_bg
 *   disabled      disabled text, 4.5:1 on the background
 *   bar           the browser bar: bg, or a step toward surf when bg is white
 *   input         the URL and search field: a step lighter than the bar
 *   bar_text, input_text, tab_text, tab_active_text, tab_hover_text, hover_text
 *                 text chosen for each of those surfaces
 *   glyph_hover   the accent as a glyph on the bar, moved toward the text only until
 *                 it reads 3:1 on both the bar and the field
 *
 * Adwaita tones, for sets drawn after GNOME's Adwaita (themer_default):
 *   view          views and fields (Adwaita's base color): toward white on light
 *                 themes, a little darker than the window on dark ones
 *   raised        header bars, popovers, cards, dialogs: as light as a view on light
 *                 themes, a step toward the text on dark ones
 *   sidebar       sidebars: a step toward the text
 *   control, control_hover, control_active
 *                 buttons and other controls, as libadwaita draws them: the text
 *                 color over the window at 10%, 20% and 30% */
#include <stdio.h>
#include <string.h>
#include "themer.h"

/* the padding scale every module uses, so spacing matches across the apps */
#define PAD_CONTROL "4px 12px"  /* buttons, tabs, menu items */
#define PAD_FIELD   "4px 6px"   /* entries, spin buttons */
#define PAD_CELL    "3px 6px"   /* list and tree cells */
#define PAD_SURFACE "2px 0"     /* menus, popovers */
#define PAD_TIP     "4px 8px"   /* tooltips */

enum {
	K_FACE, K_LIGHT, K_SHADE, K_PRESSED, K_FIELD, K_HOVER_BG, K_HOVER_FG, K_DISABLED,
	K_BAR, K_INPUT, K_BAR_TEXT, K_INPUT_TEXT, K_TAB_TEXT, K_TAB_ACTIVE_TEXT,
	K_TAB_HOVER_TEXT, K_HOVER_TEXT, K_GLYPH_HOVER, K_VIEW, K_RAISED, K_SIDEBAR,
	K_CONTROL, K_CONTROL_HOVER, K_CONTROL_ACTIVE, K_N
};

_Static_assert(K_N <= (int)(sizeof ((SharedStore *)0)->color / sizeof ((SharedStore *)0)->color[0]),
               "SharedStore.color holds every derived tone");

static void
derive(char c[][TM_COLOR_LEN], const TmTheme *t)
{
	const char *hi = t->dark ? t->fg : t->bg;
	const char *lo = t->dark ? t->bg : t->fg;
	char step[TM_COLOR_LEN];

	snprintf(c[K_FACE], TM_COLOR_LEN, "%s", t->surf);
	tm_mix(c[K_LIGHT], TM_COLOR_LEN, t->surf, hi, 4);
	tm_mix(c[K_SHADE], TM_COLOR_LEN, t->surf, lo, 5);
	tm_mix(c[K_PRESSED], TM_COLOR_LEN, t->surf, lo, 2);
	snprintf(c[K_FIELD], TM_COLOR_LEN, "%s", t->bg);
	tm_ensure_contrast(c[K_HOVER_BG], TM_COLOR_LEN, t->acc, t->bg, t->fg, 3.0);
	tm_text_on(c[K_HOVER_FG], TM_COLOR_LEN, c[K_HOVER_BG], t->accfg);
	tm_ensure_contrast(c[K_DISABLED], TM_COLOR_LEN, t->bg, t->bg, t->fg, 4.5);

	/* The browser bar and the search field. The field is a step lighter than the
	 * bar: toward the foreground on dark themes, toward white on light. A bar that
	 * is already white cannot have a lighter field, so there the bar takes a slight
	 * tint of the surface color and the field stays white. */
	snprintf(c[K_BAR], TM_COLOR_LEN, "%s", t->bg);
	if (t->dark) {
		tm_mix(c[K_INPUT], TM_COLOR_LEN, t->bg, t->fg, 2);
	} else {
		tm_mix(c[K_INPUT], TM_COLOR_LEN, t->bg, "ffffff", 3);
		if (tm_contrast(t->bg, "ffffff") < 1.05) {
			tm_mix(c[K_BAR], TM_COLOR_LEN, t->bg, t->surf, 2);
			snprintf(c[K_INPUT], TM_COLOR_LEN, "ffffff");
		}
	}
	tm_text_on(c[K_BAR_TEXT], TM_COLOR_LEN, c[K_BAR], t->fg);
	tm_text_on(c[K_INPUT_TEXT], TM_COLOR_LEN, c[K_INPUT], t->fg);
	tm_text_on(c[K_TAB_TEXT], TM_COLOR_LEN, t->surf, t->fg);
	tm_text_on(c[K_TAB_ACTIVE_TEXT], TM_COLOR_LEN, t->acc, t->accfg);
	tm_text_on(c[K_TAB_HOVER_TEXT], TM_COLOR_LEN, t->line, t->fg);
	tm_text_on(c[K_HOVER_TEXT], TM_COLOR_LEN, t->acc, t->accfg);
	/* the glyph sits on the bar and on the search field, so it must read on both */
	tm_ensure_contrast(step, sizeof step, t->acc, t->surf, t->fg, 3.0);
	tm_ensure_contrast(c[K_GLYPH_HOVER], TM_COLOR_LEN, step, c[K_BAR], t->fg, 3.0);
	/* the Adwaita tones */
	if (t->dark) {
		tm_mix(c[K_VIEW], TM_COLOR_LEN, t->bg, "000000", 2);
		tm_mix(c[K_RAISED], TM_COLOR_LEN, t->bg, t->fg, 1);
	} else {
		tm_mix(c[K_VIEW], TM_COLOR_LEN, t->bg, "ffffff", 6);
		snprintf(c[K_RAISED], TM_COLOR_LEN, "%s", c[K_VIEW]);
	}
	tm_mix(c[K_SIDEBAR], TM_COLOR_LEN, t->bg, t->fg, 1);
	tm_mix(c[K_CONTROL], TM_COLOR_LEN, t->bg, t->fg, 1);
	tm_mix(c[K_CONTROL_HOVER], TM_COLOR_LEN, t->bg, t->fg, 2);
	tm_mix(c[K_CONTROL_ACTIVE], TM_COLOR_LEN, t->bg, t->fg, 3);
}

/* Fills v with every shared name and its value, and returns how many. The values
 * point into st, which must outlive them. */
int
shared_vars(const TmTheme *t, SharedStore *st, TmVar *v, int max)
{
	char (*c)[TM_COLOR_LEN] = st->color;
	int n = 0, i;

	if (max < MAX_SHARED)
		return 0;
	derive(c, t);
	snprintf(st->num[0], sizeof st->num[0], "%d", t->ui_size);
	snprintf(st->num[1], sizeof st->num[1], "%d", t->mono_size);
	snprintf(st->num[2], sizeof st->num[2], "%d", t->cursor_size);
	snprintf(st->num[3], sizeof st->num[3], "%d", t->antialias ? 1 : 0);

#define SV(nm, val) (v[n].name = #nm, v[n].value = (val), n++)
	/* the roles */
	SV(bg, t->bg); SV(fg, t->fg); SV(surf, t->surf); SV(acc, t->acc);
	SV(accfg, t->accfg); SV(urg, t->urg); SV(warn, t->warn); SV(line, t->line);
	SV(on_urg, t->on_urg); SV(on_warn, t->on_warn);
	/* the Chicago tones */
	SV(face, c[K_FACE]); SV(light, c[K_LIGHT]); SV(shade, c[K_SHADE]);
	SV(pressed, c[K_PRESSED]); SV(field, c[K_FIELD]);
	SV(hover_bg, c[K_HOVER_BG]); SV(hover_fg, c[K_HOVER_FG]); SV(disabled, c[K_DISABLED]);
	SV(bar, c[K_BAR]); SV(input, c[K_INPUT]);
	SV(bar_text, c[K_BAR_TEXT]); SV(input_text, c[K_INPUT_TEXT]); SV(tab_text, c[K_TAB_TEXT]);
	SV(tab_active_text, c[K_TAB_ACTIVE_TEXT]); SV(tab_hover_text, c[K_TAB_HOVER_TEXT]);
	SV(hover_text, c[K_HOVER_TEXT]); SV(glyph_hover, c[K_GLYPH_HOVER]);
	/* the Adwaita tones */
	SV(view, c[K_VIEW]); SV(raised, c[K_RAISED]); SV(sidebar, c[K_SIDEBAR]);
	SV(control, c[K_CONTROL]); SV(control_hover, c[K_CONTROL_HOVER]);
	SV(control_active, c[K_CONTROL_ACTIVE]);
	/* the rest of the theme */
	SV(mode, t->dark ? "dark" : "light"); SV(theme, t->theme); SV(icons, t->icons);
	SV(ui_font, t->ui_font); SV(ui_size, st->num[0]);
	SV(mono_font, t->mono_font); SV(mono_size, st->num[1]); SV(tty_font, t->tty_font);
	SV(cursor, t->cursor); SV(cursor_size, st->num[2]);
	SV(hinting, t->hinting); SV(antialias, st->num[3]); SV(rgba, t->rgba);
	SV(pad_control, PAD_CONTROL); SV(pad_field, PAD_FIELD); SV(pad_cell, PAD_CELL);
	SV(pad_surface, PAD_SURFACE); SV(pad_tip, PAD_TIP);
#undef SV
	for (i = 0; i < 16; i++) {
		snprintf(st->ansi_name[i], sizeof st->ansi_name[i], "ansi%d", i);
		v[n].name = st->ansi_name[i];
		v[n].value = t->ansi[i];
		n++;
	}
	return n;
}

/* Looks up one shared name for a theme: its value goes to out. Modules use this when
 * they need a derived color, such as the icon ink, that the theme does not store. */
int
tm_shared(const TmTheme *t, const char *name, char *out, size_t n)
{
	SharedStore st;
	TmVar v[MAX_SHARED];
	int nv = shared_vars(t, &st, v, MAX_SHARED), i;

	for (i = 0; i < nv; i++)
		if (!strcmp(v[i].name, name))
			return snprintf(out, n, "%s", v[i].value) < (int)n ? 0 : -1;
	return -1;
}
