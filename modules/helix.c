/* helix.c: a Helix theme named "themer", and the line that selects it.
 *
 * Writes:
 *   ~/.config/helix/themes/themer.toml   templates/<set>/helix/themer.toml: the
 *                                        interface in the theme's roles, the syntax
 *                                        in its terminal colors
 *   ~/.config/helix/config.toml          theme = "themer": your theme line changed
 *                                        in place (put back on unload), or, when you
 *                                        have none, a marked block at the top, where
 *                                        a top-level key must go (before any table)
 * Helix reads both at start; a running one takes them with :config-reload.
 *
 * Options (after the host's mode):
 *   template  the set it is drawn from (templates/<set>/helix/) */
#include <limits.h>
#include <stdio.h>
#include "themer_module.h"

static const TmOption opts[] = {
	TM_OPT_TEMPLATE,
};

/* hx (helix on Arch), or the Flatpak */
static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("hx") || tm_have("helix") || tm_have_flatpak("com.helix_editor.Helix");
}

static int
load(const TmTheme *t)
{
	static char body[65536];
	char p[PATH_MAX];
	int r;

	if (tm_render(body, sizeof body, "helix", tm_opt(t, "helix.template"), "themer.toml", t, NULL, 0) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/helix/themes/themer.toml");
	if (tm_writef(p, "%s", body) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/helix/config.toml");
	r = tm_setline(p, "theme", "theme = \"themer\"");
	if (r == 1)   /* no theme line of yours (or no config): a block at the top */
		r = tm_blockf(p, "helix", "#", "", 1, "theme = \"themer\"");
	return r < 0 ? -1 : 0;
}

static const TmModule mod = {
	.name = "helix",
	.desc = "Helix: the themer theme, selected in config.toml",
	.templates = "helix",
	.default_on = 1,
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
