/* launcher.h: shared by modules/wmenu.c and modules/dmenu.c. Each writes an
 * executable script, ~/.config/themer/bin/themer-<menu>, from
 * templates/<set>/launcher/launcher. The script lists the executables on PATH,
 * shows them in the menu with the theme's colors and font, and runs the choice.
 * The sway and i3 modules bind $menu to the script when the module is loaded.
 *
 * A module supplies the menu program's name and its color and font arguments;
 * the template gets them as {{menu}} and {{args}}. */
#ifndef LAUNCHER_H
#define LAUNCHER_H

#include <limits.h>
#include <stdio.h>
#include "themer_module.h"

#define LAUNCHER_TEMPLATES "launcher"

static const TmOption launcher_options[] = {
	{ "font", "font for the menu", "ui|terminal", "ui", 0, 0 },
	TM_OPT_TEMPLATE,
};

/* the two template variables, for the module's own vars() and for load() */
static int
launcher_vars(const char *menu, const char *args, TmVar *v, int max)
{
	if (max < 2)
		return 0;
	v[0] = (TmVar){ "menu", menu };
	v[1] = (TmVar){ "args", args };
	return 2;
}

/* renders the script for menu with args and writes it, executable */
static int
launcher_load(const TmTheme *t, const char *menu, const char *args)
{
	char text[4096], p[PATH_MAX], rel[64], key[64];
	TmVar v[2];
	int nv = launcher_vars(menu, args, v, TM_COUNT(v));

	snprintf(key, sizeof key, "%s.template", menu);
	if (tm_render(text, sizeof text, LAUNCHER_TEMPLATES, tm_opt(t, key), "launcher", t, v, nv) < 0)
		return -1;
	snprintf(rel, sizeof rel, ".config/themer/bin/themer-%s", menu);
	tm_path(p, sizeof p, t, rel);
	return tm_writex(p, "%s", text);
}

#endif
