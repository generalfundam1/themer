/* fuzzel.c: the fuzzel launcher (Wayland), in the theme's colors and font.
 *
 * Sets keys in the user's ~/.config/fuzzel/fuzzel.ini, in place: [main] font,
 * the [colors] fuzzel draws with, and the [border]. The values come from
 * templates/<set>/fuzzel/fuzzel.ini, which lists the keys by section as fuzzel.ini
 * does; each is set with tm_ini, so unload puts the user's own values back (and
 * removes the file if themer created it). Colors are RRGGBBAA, as fuzzel reads them.
 * fuzzel reads its config each time it starts, so nothing needs reloading. With the
 * sway or i3 module, $menu can be "fuzzel".
 *
 * Options (after the host's mode):
 *   font      ui (the default) or terminal
 *   template  the set it is drawn from (templates/<set>/fuzzel/) */
#include <limits.h>
#include <stdio.h>
#include <string.h>
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
	tm_font_for(t, "fuzzel.font", fam, sizeof fam, &font_size);
	snprintf(size, sizeof size, "%d", font_size);
	v[0] = (TmVar){ "font_family", fam };
	v[1] = (TmVar){ "font_size", size };
	return 2;
}

TM_VARS(template_vars)

/* Sets every key of a rendered ini text in path: "[section]" lines open a section,
 * "key=value" lines are set in it, blank and # lines are skipped. */
static int
ini_apply(const char *path, char *text)
{
	char section[64] = "", *line, *next, *eq;
	int bad = 0;

	for (line = text; line && *line; line = next) {
		if ((next = strchr(line, '\n')))
			*next++ = '\0';
		line += strspn(line, " \t");
		if (!*line || *line == '#' || *line == ';')
			continue;
		if (*line == '[') {
			snprintf(section, sizeof section, "%.*s", (int)strcspn(line + 1, "]"), line + 1);
			continue;
		}
		if (!(eq = strchr(line, '=')))
			continue;
		*eq = '\0';
		line[strcspn(line, " \t")] = '\0';
		if (tm_ini(path, section, line, eq + 1 + strspn(eq + 1, " \t")) < 0)
			bad = 1;
	}
	return bad ? -1 : 0;
}

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("fuzzel");
}

static int
load(const TmTheme *t)
{
	char p[PATH_MAX], text[4096];
	TmVar v[2];
	int nv = template_vars(t, v, TM_COUNT(v));

	if (tm_render(text, sizeof text, "fuzzel", tm_opt(t, "fuzzel.template"), "fuzzel.ini", t, v, nv) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/fuzzel/fuzzel.ini");
	return ini_apply(p, text);
}

static const TmModule mod = {
	.name = "fuzzel",
	.desc = "fuzzel: a launcher in the theme's colors and font (fuzzel.ini)",
	.templates = "fuzzel",
	.default_on = 1,
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
