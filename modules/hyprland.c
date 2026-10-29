/* hyprland.c: Hyprland's window borders, groups and groupbar, background and font.
 *
 * Hyprland reads ~/.config/hypr/hyprland.lua when it is there (0.55 and later) and
 * hyprland.conf, the older hyprlang syntax, otherwise. For each of the two the user
 * has, this module writes a file of its own beside it and loads it from the end of
 * the user's config, so it is read last and only the settings it names change:
 *   hyprland.lua   ~/.config/hypr/themer.lua, from templates/<set>/hyprland/themer.lua,
 *                  loaded with pcall(require, "themer") (a missing file is no error)
 *   hyprland.conf  ~/.config/hypr/themer.conf, from templates/<set>/hyprland/themer.conf,
 *                  loaded with source =
 * With neither, nothing is written: Hyprland makes its example config on first start,
 * and the next apply hooks into that. Hyprland re-reads its config when a file
 * changes; reload() asks it too, through hyprctl, inside a Hyprland session.
 *
 * What it sets: the border colors (active, inactive, the no-group ones), the group
 * borders, the groupbar (its bars, its text, its font), the background behind the
 * windows (misc:background_color) and the default font (misc:font_family). Themer
 * Default keeps Hyprland's own look, its gradient border included, in the theme's
 * colors; Themer Chicago is square and flat: solid borders, no rounding, no shadow.
 *
 * Options:
 *   font      ui or terminal: the groupbar titles and Hyprland's default font
 *   template  the set under templates/hyprland/ */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "themer_module.h"

static const TmOption opts[] = {
	{ "font", "font for the groupbar titles and Hyprland's own text", "ui|terminal", "ui", 0, 0 },
	TM_OPT_TEMPLATE,
};

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("Hyprland") || tm_have("hyprland") || tm_have("start-hyprland");
}

/* the font, as the templates name it */
static int
vars(const TmTheme *t, TmVar *v, int max)
{
	static char fam[128], size[16];
	int sz;

	if (max < 2)
		return 0;
	tm_font_for(t, "hyprland.font", fam, sizeof fam, &sz);
	snprintf(size, sizeof size, "%d", sz);
	v[0] = (TmVar){ "font_family", fam };
	v[1] = (TmVar){ "font_size", size };
	return 2;
}

/* renders file into ~/.config/hypr/<file> */
static int
write_one(const TmTheme *t, const char *file)
{
	char body[16384], p[PATH_MAX], rel[64];
	TmVar v[2];
	int nv = vars(t, v, TM_COUNT(v));

	if (tm_render(body, sizeof body, "hyprland", tm_opt(t, "hyprland.template"), file, t, v, nv) < 0)
		return -1;
	snprintf(rel, sizeof rel, ".config/hypr/%s", file);
	tm_path(p, sizeof p, t, rel);
	return tm_writef(p, "%s", body);
}

static int
load(const TmTheme *t)
{
	char lua[PATH_MAX], conf[PATH_MAX];
	int any = 0;

	tm_path(lua, sizeof lua, t, ".config/hypr/hyprland.lua");
	tm_path(conf, sizeof conf, t, ".config/hypr/hyprland.conf");
	if (!access(lua, F_OK)) {
		if (write_one(t, "themer.lua") < 0 ||
		    tm_blockf(lua, "hyprland", "--", "", 0, "pcall(require, \"themer\")\n") < 0)
			return -1;
		any = 1;
	}
	if (!access(conf, F_OK)) {
		if (write_one(t, "themer.conf") < 0 ||
		    tm_blockf(conf, "hyprland", "#", "", 0, "source = %s/.config/hypr/themer.conf\n", t->home) < 0)
			return -1;
		any = 1;
	}
	if (!any)
		fprintf(stderr, "themer: hyprland: no ~/.config/hypr/hyprland.lua or hyprland.conf yet "
		        "(Hyprland makes one on its first start); apply again after\n");
	return 0;
}

/* only inside a Hyprland session, which sets the instance signature */
static void
reload(void)
{
	static const char *const argv[] = { "hyprctl", "reload", NULL };

	if (getenv("HYPRLAND_INSTANCE_SIGNATURE") && tm_have("hyprctl"))
		tm_run(argv);
}

TM_VARS(vars)

static const TmModule mod = {
	.name = "hyprland",
	.desc = "Hyprland: window borders, groups and the groupbar, the background, the font",
	.templates = "hyprland",
	.default_on = 1,
	.present = present,
	.load = load,
	.reload = reload,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
