/* labwc.c: labwc's window decorations, menus and on-screen displays, and its fonts.
 *
 * Two files of the user's, each hooked into with a marked block that unload cuts
 * out again:
 *   ~/.config/labwc/themerc-override  labwc's documented way to change a theme's
 *                                     colors: the theme's keys (templates/<set>/labwc/
 *                                     themerc) go at the end of the file, so they win
 *                                     over the theme and over earlier lines; whatever
 *                                     theme rc.xml names stays the base
 *   ~/.config/labwc/rc.xml            the fonts (templates/<set>/labwc/theme.xml, the
 *                                     inside of <theme>), after the last <font> of the
 *                                     user's <theme> so they win; Themer Chicago adds
 *                                     square corners there. With no rc.xml, one holding
 *                                     only that is made, and taken away on unload.
 * reload() asks a running labwc to re-read both (labwc --reconfigure, inside a labwc
 * session, which sets LABWC_PID).
 *
 * Options:
 *   font      ui or terminal: window titles, menus and the on-screen displays
 *   template  the set under templates/labwc/ */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "themer_module.h"

static const TmOption opts[] = {
	{ "font", "font for window titles, menus and the on-screen displays", "ui|terminal", "ui", 0, 0 },
	TM_OPT_TEMPLATE,
};

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("labwc");
}

static int
vars(const TmTheme *t, TmVar *v, int max)
{
	static char fam[128], size[16];
	int sz;

	if (max < 2)
		return 0;
	tm_font_for(t, "labwc.font", fam, sizeof fam, &sz);
	snprintf(size, sizeof size, "%d", sz);
	v[0] = (TmVar){ "font_family", fam };
	v[1] = (TmVar){ "font_size", size };
	return 2;
}

/* whether a line of path starts with word (after blanks), followed by a blank or the
 * end of the line: how tm_blockf_after finds its line */
static int
has_line(const char *path, const char *word)
{
	char line[1024];
	size_t n = strlen(word);
	int found = 0;
	FILE *f = fopen(path, "r");

	if (!f)
		return 0;
	while (!found && fgets(line, sizeof line, f)) {
		const char *s = line + strspn(line, " \t");

		found = !strncmp(s, word, n) && strchr(" \t\r\n", s[n]) && s[n];
	}
	fclose(f);
	return found;
}

/* the fonts block goes in <theme>, after the user's own fonts so that it wins: after
 * the last </font>, the last <font .../>, or <theme>; in a config without <theme>, a
 * <theme> of its own after <labwc_config> */
static int
fonts_block(const char *rc, const char *inner)
{
	static const char *const anchors[] = { "</font>", "<font", "<theme>" };

	if (access(rc, F_OK))
		return tm_blockf(rc, "labwc", "<!--", " -->", 0,
		                 "<labwc_config>\n  <theme>\n%s  </theme>\n</labwc_config>\n", inner);
	for (size_t i = 0; i < TM_COUNT(anchors); i++)
		if (has_line(rc, anchors[i]))
			return tm_blockf_after(rc, "labwc", "<!--", " -->", anchors[i], "%s", inner);
	if (has_line(rc, "<labwc_config>"))
		return tm_blockf_after(rc, "labwc", "<!--", " -->", "<labwc_config>",
		                       "  <theme>\n%s  </theme>\n", inner);
	fprintf(stderr, "themer: labwc: no <labwc_config> or <theme> in rc.xml; fonts not set\n");
	return 0;
}

static int
load(const TmTheme *t)
{
	const char *set = tm_opt(t, "labwc.template");
	char body[8192], inner[2048], p[PATH_MAX];
	TmVar v[2];
	int nv = vars(t, v, TM_COUNT(v));

	if (tm_render(body, sizeof body, "labwc", set, "themerc", t, v, nv) < 0 ||
	    tm_render(inner, sizeof inner, "labwc", set, "theme.xml", t, v, nv) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/labwc/themerc-override");
	if (tm_blockf(p, "labwc", "#", "", 0, "%s", body) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/labwc/rc.xml");
	return fonts_block(p, inner);
}

/* inside a labwc session */
static void
reload(void)
{
	static const char *const argv[] = { "labwc", "--reconfigure", NULL };

	if (getenv("LABWC_PID") && tm_have("labwc"))
		tm_run(argv);
}

TM_VARS(vars)

static const TmModule mod = {
	.name = "labwc",
	.desc = "labwc: window decorations, menus and on-screen displays, the fonts",
	.templates = "labwc",
	.default_on = 1,
	.present = present,
	.load = load,
	.reload = reload,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
