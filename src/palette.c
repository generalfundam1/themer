/* palette.c: the shared template variables. Every template, whichever module
 * renders it, sees the same names: the theme's roles, the Chicago tones derived
 * from them, the fonts, the terminal colors, and the padding scale. The derived
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
 *                 it reads 3:1 on both the bar and the field */
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
	K_TAB_HOVER_TEXT, K_HOVER_TEXT, K_GLYPH_HOVER, K_N
};

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
