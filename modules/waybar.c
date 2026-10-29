/* waybar.c: the bar's look, from the template set (Themer Default's after GNOME's
 * top bar, Themer Chicago's dwm-style), in the terminal font by default.
 *
 * The module owns ~/.config/waybar/style.css. Your original is kept as
 * style.css.themer-orig and restored on unload, byte for byte. config.jsonc
 * is left alone, so your modules and format strings stay as they are.
 *
 * The look is in templates/<set>/waybar/style.css. This module supplies the font
 * and the padding: "padding" picks the spacing around workspaces and status
 * modules, in pixels. */
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "themer_module.h"

static const TmOption opts[] = {
	{ "font",    "font for the bar", "ui|terminal", "terminal", 0, 0 },
	{ "padding", "space around workspaces and status modules", "tight|normal|loose", "normal", 0, 0 },
	TM_OPT_TEMPLATE,
};

/* the padding in pixels for the workspace boxes (ws) and status modules (mod) */
static void
spacing(const TmTheme *t, int *ws, int *mod)
{
	const char *p = tm_opt(t, "waybar.padding");

	if (p && !strcmp(p, "tight")) {
		*ws = 4;
		*mod = 1;
	} else if (p && !strcmp(p, "loose")) {
		*ws = 12;
		*mod = 4;
	} else {
		*ws = 7;
		*mod = 2;
	}
}

/* the module's own variables: the font, and the two paddings */
static int
template_vars(const TmTheme *t, TmVar *v, int max)
{
	static char fam[128], size[16], ws[16], mod[16];
	int font_size, pad_ws, pad_mod;

	if (max < 4)
		return 0;
	tm_font_for(t, "waybar.font", fam, sizeof fam, &font_size);
	spacing(t, &pad_ws, &pad_mod);
	snprintf(size, sizeof size, "%d", font_size);
	snprintf(ws, sizeof ws, "%d", pad_ws);
	snprintf(mod, sizeof mod, "%d", pad_mod);
	v[0] = (TmVar){ "font_family", fam };
	v[1] = (TmVar){ "font_size", size };
	v[2] = (TmVar){ "ws_pad", ws };
	v[3] = (TmVar){ "mod_pad", mod };
	return 4;
}

TM_VARS(template_vars)

static int
load(const TmTheme *t)
{
	char cfg[PATH_MAX], css[PATH_MAX], body[8192];
	TmVar v[4];
	int nv;

	tm_path(cfg, sizeof cfg, t, ".config/waybar/config.jsonc");

	nv = template_vars(t, v, TM_COUNT(v));
	if (tm_render(body, sizeof body, "waybar", tm_opt(t, "waybar.template"), "style.css",
	              t, v, nv) < 0)
		return -1;
	tm_path(css, sizeof css, t, ".config/waybar/style.css");
	return tm_replacef(css, "%s", body);
}

/* Sends SIGUSR2 (re-read style.css) to each running waybar, found through /proc (tm_signal).
 * Nothing is started: a bar that is not running is simply not signalled. */
static int
present(const TmTheme *t)
{
	char cfg[PATH_MAX];

	tm_path(cfg, sizeof cfg, t, ".config/waybar/config.jsonc");
	return !access(cfg, F_OK) || tm_have("waybar");
}

static void
reload(void)
{
	tm_signal("waybar", SIGUSR2);
}

static const TmModule mod = {
	.name = "waybar",
	.desc = "Waybar: the bar's style; under sway it takes the place of sway's own bar",
	.templates = "waybar",
	.default_on = 1,
	.present = present,
	.load = load,
	.reload = reload,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
