/* i3.c: i3 window and bar colors and font. The writer, the options, and the
 * templates (templates/wm/) are shared with sway through wm.h. Loads only where i3
 * is in use (its socket, or its config). */
#include "wm.h"

static int
present(const TmTheme *t)
{
	return wm_present(t, "i3", "I3SOCK");
}

static int
load(const TmTheme *t)
{
	return wm_load(t, "i3", "I3SOCK");
}

static int
vars(const TmTheme *t, TmVar *v, int max)
{
	return wm_vars(t, "i3", v, max);
}

static void
reload(void)
{
	wm_reload("I3SOCK", "i3-msg reload >/dev/null 2>&1");
}

TM_VARS(vars)

static const TmModule mod = {
	.name = "i3",
	.desc = "i3: window and bar colors, the font, the launcher",
	.templates = WM_TEMPLATES,
	.default_on = 1,
	.present = present,
	.load = load,
	.reload = reload,
	.options = wm_options,
	.noptions = TM_COUNT(wm_options),
	.watches_modules = 1,
};
TM_EXPORT(mod)
