/* dmenu.c: the dmenu launcher, with the theme's colors and font. The script and
 * its template (templates/launcher/) are shared with wmenu through launcher.h. */
#include "launcher.h"

/* dmenu's flags: normal and selected colors, and the font */
static const char *
args(const TmTheme *t)
{
	static char buf[512];
	char fam[128];
	int size;

	tm_font_for(t, "dmenu.font", fam, sizeof fam, &size);
	snprintf(buf, sizeof buf, "-nb %s -nf %s -sb %s -sf %s -fn \"%s %d\"",
	         t->bg, t->fg, t->acc, t->accfg, fam, size);
	return buf;
}

static int
vars(const TmTheme *t, TmVar *v, int max)
{
	return launcher_vars("dmenu", args(t), v, max);
}

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("dmenu");
}

static int
load(const TmTheme *t)
{
	return launcher_load(t, "dmenu", args(t));
}

TM_VARS(vars)

static const TmModule mod = {
	.name = "dmenu",
	.desc = "dmenu: a launcher in the theme's colors, used as $menu by sway and i3",
	.templates = LAUNCHER_TEMPLATES,
	.default_on = 1,
	.present = present,
	.load = load,
	.options = launcher_options,
	.noptions = TM_COUNT(launcher_options),
};
TM_EXPORT(mod)
