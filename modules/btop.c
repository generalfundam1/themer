/* btop.c: btop's colors, as a theme of its own.
 *
 * Writes ~/.config/btop/themes/themer.theme, rendered from templates/<set>/btop/
 * themer.theme: btop's default look hue for hue, its boxes, meters and graphs in
 * the theme's terminal colors. btop reads themes from that directory and is pointed
 * at this one by the color_theme line of ~/.config/btop/btop.conf, which is changed
 * in place: btop rewrites btop.conf itself whenever a setting changes, keeping the
 * line where it is, so a marked block would not last. The set's btop/settings may
 * change more lines the same way (Themer Chicago squares the boxes with
 * rounded_corners = False). A line btop.conf does not have yet (it has none until
 * btop first runs) goes into a marked block at its end. Unload puts the old lines
 * back.
 *
 * A running btop keeps its theme until it is restarted, or until another theme is
 * picked in its options menu and this one picked again.
 *
 * Options (after the host's mode):
 *   template  the set it is drawn from (templates/<set>/btop/) */
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "themer_module.h"

static const TmOption opts[] = {
	TM_OPT_TEMPLATE,
};

/* dim1..dim6: a terminal color a little way out of the background, where btop's own
 * gradients start (its meters fade in from near the background) */
static int
template_vars(const TmTheme *t, TmVar *v, int max)
{
	static char dim[7][TM_COLOR_LEN], names[7][8];
	int n = 0;

	for (int i = 1; i <= 6 && n < max; i++) {
		tm_mix(dim[i], sizeof dim[i], t->bg, t->ansi[i], 4);
		snprintf(names[i], sizeof names[i], "dim%d", i);
		v[n++] = (TmVar){ names[i], dim[i] };
	}
	return n;
}

TM_VARS(template_vars)

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("btop");
}

/* one "key = value" line: changed in place when btop.conf has the key, else added to
 * the block's text */
static void
setting(const char *conf, const char *line, char *block, size_t n)
{
	char key[64];
	size_t k = strcspn(line, " =");

	if (!k || k >= sizeof key)
		return;
	snprintf(key, sizeof key, "%.*s", (int)k, line);
	if (tm_setline(conf, key, line) == 1)
		snprintf(block + strlen(block), n - strlen(block), "%s\n", line);
}

static int
load(const TmTheme *t)
{
	static char theme[16384], settings[4096];
	char p[PATH_MAX], conf[PATH_MAX], block[4096] = "", *line, *save;
	const char *set = tm_opt(t, "btop.template");
	TmVar v[8];
	int nv = template_vars(t, v, TM_COUNT(v));

	if (tm_render(theme, sizeof theme, "btop", set, "themer.theme", t, v, nv) < 0 ||
	    tm_render(settings, sizeof settings, "btop", set, "settings", t, v, nv) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/btop/themes/themer.theme");
	if (tm_writef(p, "%s", theme) < 0)
		return -1;

	tm_path(conf, sizeof conf, t, ".config/btop/btop.conf");
	setting(conf, "color_theme = \"themer\"", block, sizeof block);
	for (line = strtok_r(settings, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
		if (line[0] && line[0] != '#')
			setting(conf, line, block, sizeof block);
	if (block[0])
		return tm_blockf(conf, "btop", "#", "", 0, "%s", block);
	return 0;
}

static const TmModule mod = {
	.name = "btop",
	.desc = "btop: the system monitor's colors, as a theme of its own (btop.conf's color_theme)",
	.templates = "btop",
	.default_on = 1,
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
