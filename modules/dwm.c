/* dwm.c: dwm has no runtime config; its colors and fonts are compiled in.
 * This module writes ~/.config/themer/dwm/colors.h (from templates/<set>/dwm/colors.h),
 * which defines fonts[], dmenufont[] and colors[][3] the way dwm's config.def.h does.
 * To use it, edit dwm's config.h once: delete its fonts[], dmenufont[], col_* and
 * colors[][3] lines and put
 *   #include "themer-colors.h"
 * in their place. The header also goes to $DWM_SRC/themer-colors.h when DWM_SRC
 * names dwm's source directory; otherwise copy or link it there. Rebuild dwm after
 * an apply. dwm's own look: the bar and unfocused borders in the background and line
 * colors, the selected tag, title and border in the accent (dwm's cyan).
 *
 * Options:
 *   font      ui or terminal: the bar's font (and dmenu's, when dwm starts it)
 *   template  the set under templates/dwm/ */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include "themer_module.h"

static const TmOption opts[] = {
	{ "font", "font for the bar (and dmenu, started from dwm)", "ui|terminal", "terminal", 0, 0 },
	TM_OPT_TEMPLATE,
};

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("dwm");
}

static int
vars(const TmTheme *t, TmVar *v, int max)
{
	static char fam[128], size[16];
	int sz;

	if (max < 2)
		return 0;
	tm_font_for(t, "dwm.font", fam, sizeof fam, &sz);
	snprintf(size, sizeof size, "%d", sz);
	v[0] = (TmVar){ "font_family", fam };
	v[1] = (TmVar){ "font_size", size };
	return 2;
}

static int
load(const TmTheme *t)
{
	char body[4096], p[PATH_MAX];
	const char *src = getenv("DWM_SRC");
	TmVar v[2];
	int nv = vars(t, v, TM_COUNT(v));

	if (tm_render(body, sizeof body, "dwm", tm_opt(t, "dwm.template"), "colors.h", t, v, nv) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/themer/dwm/colors.h");
	if (tm_writef(p, "%s", body) < 0)
		return -1;
	if (src && *src) {
		snprintf(p, sizeof p, "%s/themer-colors.h", src);
		return tm_writef(p, "%s", body);
	}
	return 0;
}

TM_VARS(vars)

static const TmModule mod = {
	.name = "dwm",
	.desc = "dwm: a colors and fonts header (dwm's config.h needs one edit, see modules/dwm.c)",
	.templates = "dwm",
	.default_on = 1,
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
