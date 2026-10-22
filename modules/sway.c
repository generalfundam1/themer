/* sway.c: sway window and bar colors and font. The writer, the options, and
 * the templates (templates/wm/) are shared with i3 through wm.h. Loads only where
 * sway is in use (its socket, or its config). */
#include "wm.h"

static int
present(const TmTheme *t)
{
	return wm_present(t, "sway", "SWAYSOCK");
}

static int
load(const TmTheme *t)
{
	return wm_load(t, "sway", "SWAYSOCK");
}

static int
vars(const TmTheme *t, TmVar *v, int max)
{
	return wm_vars(t, "sway", v, max);
}

static void
reload(void)
{
	wm_reload("SWAYSOCK", "swaymsg reload >/dev/null 2>&1");
}

TM_VARS(vars)

static const TmModule mod = {
	.name = "sway",
	.desc = "sway: window and bar colors, the font, $menu, the seat cursor",
	.templates = WM_TEMPLATES,
	.default_on = 1,
	.present = present,
	.load = load,
	.reload = reload,
	.options = wm_options,
	.noptions = TM_COUNT(wm_options),
};
TM_EXPORT(mod)
