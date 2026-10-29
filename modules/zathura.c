/* zathura.c: the document viewer's colors and font.
 *
 * Renders templates/<set>/zathura/zathurarc (a list of "set" lines) into a marked
 * block at the end of ~/.config/zathura/zathurarc, where it is read last and so
 * overrides the same settings above it; the rest of the file stays the user's.
 * The page area and the status bar sit on the background, the input bar and the
 * completion lists on the surface, search hits in the accent. Pages keep their own
 * colors unless zathura's recolor mode is on (Ctrl-R): then they are drawn in the
 * theme's text and background colors.
 *
 * A running zathura reads the file again on :source.
 *
 * Options (after the host's mode):
 *   font      the theme's terminal font (zathura's own default is monospace) or its
 *             interface font
 *   template  the set it is drawn from (templates/<set>/zathura/) */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include "themer_module.h"

static const TmOption opts[] = {
	{ "font", "font for the status bar, the input bar and the index", "terminal|ui", "terminal", 0, 0 },
	TM_OPT_TEMPLATE,
};

/* font: a Pango description; acc_soft and acc_strong: the accent see-through, for
 * search hits over the page (zathura takes rgba() here, not hex with alpha) */
static int
template_vars(const TmTheme *t, TmVar *v, int max)
{
	static char font[160], soft[48], strong[48];
	char fam[128];
	int size;
	unsigned r = 0, g = 0, b = 0;

	if (max < 3)
		return 0;
	tm_font_for(t, "zathura.font", fam, sizeof fam, &size);
	snprintf(font, sizeof font, "%s %d", fam, size);
	sscanf(t->acc, "%2x%2x%2x", &r, &g, &b);
	snprintf(soft, sizeof soft, "rgba(%u,%u,%u,0.4)", r, g, b);
	snprintf(strong, sizeof strong, "rgba(%u,%u,%u,0.7)", r, g, b);
	v[0] = (TmVar){ "font", font };
	v[1] = (TmVar){ "acc_soft", soft };
	v[2] = (TmVar){ "acc_strong", strong };
	return 3;
}

TM_VARS(template_vars)

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("zathura");
}

static int
load(const TmTheme *t)
{
	static char body[8192];
	char p[PATH_MAX];
	TmVar v[3];
	int nv = template_vars(t, v, TM_COUNT(v));

	if (tm_render(body, sizeof body, "zathura", tm_opt(t, "zathura.template"), "zathurarc", t, v, nv) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/zathura/zathurarc");
	return tm_blockf(p, "zathura", "#", "", 0, "%s", body);
}

static const TmModule mod = {
	.name = "zathura",
	.desc = "zathura: the document viewer's colors and font (zathurarc)",
	.templates = "zathura",
	.default_on = 1,
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
