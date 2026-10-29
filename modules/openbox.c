/* openbox.c: an Openbox theme in the theme's colors, selected in rc.xml, and the fonts.
 *
 * Writes the theme, "Themer", to ~/.local/share/themes/Themer/openbox-3/themerc (from
 * templates/<set>/openbox/themerc), and hooks into the user's ~/.config/openbox/rc.xml
 * with two marked blocks that unload cuts out again:
 *   right after <theme>        <name>Themer</name>: Openbox takes the first <name>
 *                              in <theme>, so this one is used and the user's own
 *                              <name> line stays as it was
 *   after the last </font>     the fonts (templates/<set>/openbox/fonts.xml): Openbox
 *                              reads the fonts in order, so these win
 * Without a ~/.config/openbox/rc.xml (Openbox then reads the system's), the theme is
 * written and nothing else: copy /etc/xdg/openbox/rc.xml there and apply again.
 * reload() asks a running Openbox to re-read (openbox --reconfigure).
 *
 * Options:
 *   font      ui or terminal: window titles, menus and the on-screen displays
 *   template  the set under templates/openbox/ */
#include <limits.h>
#include <stdio.h>
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
	return tm_have("openbox");
}

/* the font, and the accent a step lighter and darker for the title gradients */
static int
vars(const TmTheme *t, TmVar *v, int max)
{
	static char fam[128], size[16], hi[TM_COLOR_LEN], lo[TM_COLOR_LEN];
	int sz;

	if (max < 4)
		return 0;
	tm_font_for(t, "openbox.font", fam, sizeof fam, &sz);
	snprintf(size, sizeof size, "%d", sz);
	tm_mix(hi, sizeof hi, t->acc, "ffffff", 3);
	tm_mix(lo, sizeof lo, t->acc, "000000", 2);
	v[0] = (TmVar){ "font_family", fam };
	v[1] = (TmVar){ "font_size", size };
	v[2] = (TmVar){ "acc_hi", hi };
	v[3] = (TmVar){ "acc_lo", lo };
	return 4;
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

		found = !strncmp(s, word, n) && s[n] && strchr(" \t\r\n", s[n]);
	}
	fclose(f);
	return found;
}

static int
load(const TmTheme *t)
{
	const char *set = tm_opt(t, "openbox.template");
	char body[16384], fonts[4096], p[PATH_MAX];
	TmVar v[4];
	int nv = vars(t, v, TM_COUNT(v));

	if (tm_render(body, sizeof body, "openbox", set, "themerc", t, v, nv) < 0 ||
	    tm_render(fonts, sizeof fonts, "openbox", set, "fonts.xml", t, v, nv) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".local/share/themes/Themer/openbox-3/themerc");
	if (tm_writef(p, "%s", body) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/openbox/rc.xml");
	if (access(p, F_OK)) {
		fprintf(stderr, "themer: openbox: no ~/.config/openbox/rc.xml; copy /etc/xdg/openbox/rc.xml "
		        "there and apply again to select the Themer theme\n");
		return 0;
	}
	if (!has_line(p, "<theme>")) {
		fprintf(stderr, "themer: openbox: no <theme> in rc.xml; select the Themer theme yourself\n");
		return 0;
	}
	if (tm_blockf_after(p, "openbox-fonts", "<!--", " -->", has_line(p, "</font>") ? "</font>" : "<theme>",
	                    "%s", fonts) < 0)
		return -1;
	return tm_blockf_after(p, "openbox", "<!--", " -->", "<theme>", "    <name>Themer</name>\n");
}

/* while Openbox runs */
static void
reload(void)
{
	static const char *const argv[] = { "openbox", "--reconfigure", NULL };

	if (tm_signal("openbox", 0))
		tm_run(argv);
}

TM_VARS(vars)

static const TmModule mod = {
	.name = "openbox",
	.desc = "Openbox: a theme in the theme's colors, selected in rc.xml, and the fonts",
	.templates = "openbox",
	.default_on = 1,
	.present = present,
	.load = load,
	.reload = reload,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
