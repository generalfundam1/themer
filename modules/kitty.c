/* kitty.c: kitty's colors, font and tab bar.
 *
 * Writes ~/.config/kitty/themer.conf, rendered from templates/<set>/kitty/themer.conf,
 * and includes it from a marked block at the end of the user's kitty.conf (created
 * when there is none), so it is read after the user's own settings. Themer Default
 * keeps kitty's own look in the theme's colors; Themer Chicago draws the tab bar as
 * flat separated tabs. Running kittys re-read their config on SIGUSR1.
 *
 * Options (after the host's mode):
 *   font      terminal (the default) or ui
 *   template  the set it is drawn from */
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include "themer_module.h"

static const TmOption opts[] = {
	{ "font", "font for the terminal", "terminal|ui", "terminal", 0, 0 },
	TM_OPT_TEMPLATE,
};

/* font_family and font_size, from the font option */
static int
template_vars(const TmTheme *t, TmVar *v, int max)
{
	static char fam[128], size[16];
	int sz;

	if (max < 2)
		return 0;
	tm_font_for(t, "kitty.font", fam, sizeof fam, &sz);
	snprintf(size, sizeof size, "%d", sz);
	v[0] = (TmVar){ "font_family", fam };
	v[1] = (TmVar){ "font_size", size };
	return 2;
}

TM_VARS(template_vars)

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("kitty");
}

static int
load(const TmTheme *t)
{
	char body[8192], p[PATH_MAX];
	TmVar v[2];
	int nv = template_vars(t, v, TM_COUNT(v));

	if (tm_render(body, sizeof body, "kitty", tm_opt(t, "kitty.template"), "themer.conf", t, v, nv) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/kitty/themer.conf");
	if (tm_writef(p, "%s", body) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/kitty/kitty.conf");
	return tm_blockf(p, "kitty", "#", "", 0, "include themer.conf");
}

static void
reload(void)
{
	tm_signal("kitty", SIGUSR1);
}

static const TmModule mod = {
	.name = "kitty",
	.desc = "kitty: the terminal's colors, font and tab bar (themer.conf, included from kitty.conf)",
	.templates = "kitty",
	.default_on = 1,
	.present = present,
	.load = load,
	.reload = reload,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
