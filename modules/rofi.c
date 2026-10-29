/* rofi.c: the rofi launcher (X11 and, from rofi 2.0, Wayland), in the theme's colors
 * and font.
 *
 * Writes a rofi theme, ~/.local/share/rofi/themes/themer.rasi, from
 * templates/<set>/rofi/themer.rasi, and selects it with a marked block at the end of
 * the user's ~/.config/rofi/config.rasi: @theme with the theme's full path. @theme
 * starts the theme afresh, so a theme chosen earlier in config.rasi gives way to it
 * while it is on, and comes back on unload, when the block is taken out (and
 * config.rasi removed if themer created it). The rest of config.rasi (the
 * configuration { } block, modes, keys) stays as it is. Rofi reads it each time it
 * starts. With the sway or i3 module, $menu can be "rofi -show drun".
 *
 * Options (after the host's mode):
 *   font      ui (the default) or terminal
 *   template  the set it is drawn from (templates/<set>/rofi/) */
#include <limits.h>
#include <stdio.h>
#include "themer_module.h"

static const TmOption opts[] = {
	{ "font", "font for the launcher", "ui|terminal", "ui", 0, 0 },
	TM_OPT_TEMPLATE,
};

/* the module's own variables: the font family and size */
static int
template_vars(const TmTheme *t, TmVar *v, int max)
{
	static char fam[128], size[16];
	int font_size;

	if (max < 2)
		return 0;
	tm_font_for(t, "rofi.font", fam, sizeof fam, &font_size);
	snprintf(size, sizeof size, "%d", font_size);
	v[0] = (TmVar){ "font_family", fam };
	v[1] = (TmVar){ "font_size", size };
	return 2;
}

TM_VARS(template_vars)

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("rofi");
}

static int
load(const TmTheme *t)
{
	char theme[PATH_MAX], conf[PATH_MAX], text[16384];
	TmVar v[2];
	int nv = template_vars(t, v, TM_COUNT(v));

	if (tm_render(text, sizeof text, "rofi", tm_opt(t, "rofi.template"), "themer.rasi", t, v, nv) < 0)
		return -1;
	tm_path(theme, sizeof theme, t, ".local/share/rofi/themes/themer.rasi");
	if (tm_writef(theme, "%s", text) < 0)
		return -1;
	tm_path(conf, sizeof conf, t, ".config/rofi/config.rasi");
	return tm_blockf(conf, "rofi", "//", "", 0, "@theme \"%s\"", theme);
}

static const TmModule mod = {
	.name = "rofi",
	.desc = "rofi: a launcher in the theme's colors and font (a theme set in config.rasi)",
	.templates = "rofi",
	.default_on = 1,
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
