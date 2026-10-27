/* newt.c: colors for newt dialogs, such as nmtui.
 * newt draws in the terminal's own 16 ANSI colors and reads them by name from
 * NEWT_COLORS. Each theme color is mapped to the nearest of the theme's ANSI
 * colors, which are the colors foot draws. The mapping is computed here; the
 * layout of the file is templates/newt/<set>/newt.conf. The variable goes in
 * environment.d, so it reaches programs started from the systemd user session.
 * Programs started another way need NEWT_COLORS set in the shell profile. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "themer_module.h"

static const TmOption opts[] = {
	TM_OPT_TEMPLATE,
};

/* newt's name for each ANSI index, 0..15 */
static const char *newt_name[16] = {
	"black", "red", "green", "brown", "blue", "magenta", "cyan", "lightgray",
	"gray", "brightred", "brightgreen", "yellow", "brightblue", "brightmagenta",
	"brightcyan", "white",
};

static long
hexval(const char *h, int shift)
{
	return (strtol(h, NULL, 16) >> shift) & 0xff;
}

/* index of the ANSI color closest to hex, by squared RGB distance */
static const char *
nearest(const char *hex, const TmTheme *t)
{
	long best = -1;
	int best_i = 0;

	for (int i = 0; i < 16; i++) {
		long d = 0;

		for (int sh = 16; sh >= 0; sh -= 8) {
			long x = hexval(hex, sh) - hexval(t->ansi[i], sh);

			d += x * x;
		}
		if (best < 0 || d < best) {
			best = d;
			best_i = i;
		}
	}
	return newt_name[best_i];
}

/* appends key=fg,bg: to the list, using the nearest ANSI names */
static void
pair(char *buf, size_t n, const TmTheme *t, const char *key, const char *fg, const char *bg)
{
	char tmp[96];

	snprintf(tmp, sizeof tmp, "%s=%s,%s:", key, nearest(fg, t), nearest(bg, t));
	strncat(buf, tmp, n - strlen(buf) - 1);
}

/* the module's own variable: the NEWT_COLORS value */
static int
template_vars(const TmTheme *t, TmVar *v, int max)
{
	static char colors[1024];

	if (max < 1)
		return 0;
	colors[0] = '\0';
	pair(colors, sizeof colors, t, "root", t->fg, t->bg);
	pair(colors, sizeof colors, t, "window", t->fg, t->bg);
	pair(colors, sizeof colors, t, "border", t->acc, t->bg);
	pair(colors, sizeof colors, t, "title", t->acc, t->bg);
	pair(colors, sizeof colors, t, "button", t->fg, t->surf);
	pair(colors, sizeof colors, t, "actbutton", t->accfg, t->acc);
	pair(colors, sizeof colors, t, "compactbutton", t->fg, t->surf);
	pair(colors, sizeof colors, t, "checkbox", t->fg, t->bg);
	pair(colors, sizeof colors, t, "actcheckbox", t->accfg, t->acc);
	pair(colors, sizeof colors, t, "entry", t->fg, t->surf);
	pair(colors, sizeof colors, t, "disentry", t->ansi[8], t->surf);
	pair(colors, sizeof colors, t, "label", t->fg, t->bg);
	pair(colors, sizeof colors, t, "listbox", t->fg, t->bg);
	pair(colors, sizeof colors, t, "actlistbox", t->accfg, t->acc);
	pair(colors, sizeof colors, t, "sellistbox", t->fg, t->surf);
	pair(colors, sizeof colors, t, "actsellistbox", t->accfg, t->acc);
	pair(colors, sizeof colors, t, "textbox", t->fg, t->bg);
	pair(colors, sizeof colors, t, "acttextbox", t->accfg, t->acc);
	pair(colors, sizeof colors, t, "helpline", t->fg, t->surf);
	pair(colors, sizeof colors, t, "roottext", t->fg, t->bg);
	pair(colors, sizeof colors, t, "emptyscale", t->ansi[8], t->bg);
	pair(colors, sizeof colors, t, "fullscale", t->acc, t->bg);
	pair(colors, sizeof colors, t, "shadow", t->ansi[8], t->bg);
	/* drop the trailing ':' */
	colors[strlen(colors) - 1] = '\0';
	v[0] = (TmVar){ "newt_colors", colors };
	return 1;
}

TM_VARS(template_vars)

static int
load(const TmTheme *t)
{
	char body[1200], p[PATH_MAX];
	TmVar v[1];
	int nv = template_vars(t, v, TM_COUNT(v));

	if (tm_render(body, sizeof body, "newt", tm_opt(t, "newt.template"), "newt.conf", t, v, nv) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/environment.d/40-themer-newt.conf");
	return tm_writef(p, "%s", body);
}

static const TmModule mod = {
	.name = "newt",
	.desc = "newt: the colors of nmtui and other newt dialogs",
	.templates = "newt",
	.default_on = 1,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
