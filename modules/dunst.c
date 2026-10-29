/* dunst.c: the dunst notification daemon, in the theme's colors and font.
 *
 * Writes a drop-in, ~/.config/dunst/dunstrc.d/90-themer.conf, from
 * templates/<set>/dunst/themer.conf: the font, the frame, the progress bar, and the
 * colors of each urgency. Dunst reads the drop-ins next to the dunstrc it uses, after
 * it, so the user's dunstrc is never edited. Dunst only reads the drop-ins of the
 * directory its dunstrc is in: without a ~/.config/dunst/dunstrc of the user's own
 * (dunst then uses the system's), one is created holding nothing but a marked
 * comment, and removed on unload. A running dunst re-reads both (dunstctl reload).
 *
 * Options (after the host's mode):
 *   font      ui (the default) or terminal
 *   template  the set it is drawn from (templates/<set>/dunst/) */
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include "themer_module.h"

static const TmOption opts[] = {
	{ "font", "font for the notifications", "ui|terminal", "ui", 0, 0 },
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
	tm_font_for(t, "dunst.font", fam, sizeof fam, &font_size);
	snprintf(size, sizeof size, "%d", font_size);
	v[0] = (TmVar){ "font_family", fam };
	v[1] = (TmVar){ "font_size", size };
	return 2;
}

TM_VARS(template_vars)

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("dunst");
}

static int
load(const TmTheme *t)
{
	char p[PATH_MAX], text[8192];
	TmVar v[2];
	int nv = template_vars(t, v, TM_COUNT(v));

	if (tm_render(text, sizeof text, "dunst", tm_opt(t, "dunst.template"), "themer.conf", t, v, nv) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/dunst/dunstrc");
	if (access(p, F_OK) && tm_blockf(p, "dunst", "#", "", 0,
	                                 "# no dunstrc of your own: this one is here so that dunst reads\n"
	                                 "# dunstrc.d/90-themer.conf; dunst's defaults apply to the rest") < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/dunst/dunstrc.d/90-themer.conf");
	return tm_writef(p, "%s", text);
}

/* a running dunst re-reads its config; signal 0 only asks whether one runs */
static void
reload(void)
{
	static const char *const argv[] = { "dunstctl", "reload", NULL };

	if (tm_signal("dunst", 0) > 0 && tm_have("dunstctl"))
		tm_run(argv);
}

static const TmModule mod = {
	.name = "dunst",
	.desc = "dunst: notifications in the theme's colors and font (a dunstrc.d drop-in)",
	.templates = "dunst",
	.default_on = 1,
	.present = present,
	.load = load,
	.reload = reload,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
