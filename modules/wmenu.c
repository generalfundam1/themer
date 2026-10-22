/* wmenu.c: the wmenu launcher, with the theme's colors and font. The script and
 * its template (templates/launcher/) are shared with dmenu through launcher.h. */
#include "launcher.h"

/* wmenu's flags: normal, selected, and match colors, and the font */
static const char *
args(const TmTheme *t)
{
	static char buf[512];
	char fam[128];
	int size;

	tm_font_for(t, "wmenu.font", fam, sizeof fam, &size);
	snprintf(buf, sizeof buf, "-N %s -n %s -M %s -m %s -S %s -s %s -f \"%s %d\"",
	         t->bg, t->fg, t->acc, t->accfg, t->surf, t->fg, fam, size);
	return buf;
}

static int
vars(const TmTheme *t, TmVar *v, int max)
{
	return launcher_vars("wmenu", args(t), v, max);
}

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("wmenu");
}

static int
load(const TmTheme *t)
{
	return launcher_load(t, "wmenu", args(t));
}

TM_VARS(vars)

static const TmModule mod = {
	.name = "wmenu",
	.desc = "wmenu launcher, colored (the script sway and i3 use as $menu)",
	.templates = LAUNCHER_TEMPLATES,
	.default_on = 1,
	.present = present,
	.load = load,
	.options = launcher_options,
	.noptions = TM_COUNT(launcher_options),
};
TM_EXPORT(mod)
