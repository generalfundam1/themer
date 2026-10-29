/* newt.c: colors for newt dialogs, such as nmtui and whiptail.
 * newt draws in the terminal's own colors and reads them by name from NEWT_COLORS:
 * the 16 ANSI colors, or "default", the terminal's own text and background. The
 * terminal modules (and the console palette) make those the theme's: default is the
 * theme's fg and bg, the 16 its terminal colors. Each pair newt draws (text on a
 * background) is mapped to the names nearest the theme's roles for it, background
 * first, among the pairs that still read (4.5:1 for text, 3:1 for the border and
 * disabled entries): a theme whose 16 colors hold nothing near its background (all
 * dark in a light theme, say) gets "default" there instead of an unrelated color,
 * and a selection whose accent has no readable text among the 16 moves to the
 * nearest background that has some. Focused things must not look like unfocused
 * ones: the selection stands out from the window's text and from the buttons, and
 * a focused compact button (whiptail's <Ok>, drawn in "button") from an unfocused
 * one ("compactbutton", the window's text). The scales take only a background
 * (newt draws them in spaces): the accent for the filled part, the surface for the
 * rest. Shadows are drawn in the background, so none shows, as nothing in the set
 * casts one. The layout of the file is templates/<set>/newt/newt.conf. The variable
 * goes in environment.d, so it reaches programs started from the systemd user
 * session; programs started another way need NEWT_COLORS set in the shell profile
 * (~/.config/themer/env.sh does it). */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "themer_module.h"

static const TmOption opts[] = {
	TM_OPT_TEMPLATE,
};

/* newt's name for each ANSI index, 0..15, and -1: the terminal's own color */
static const char *newt_name[16] = {
	"black", "red", "green", "brown", "blue", "magenta", "cyan", "lightgray",
	"gray", "brightred", "brightgreen", "yellow", "brightblue", "brightmagenta",
	"brightcyan", "white",
};

static const char *
name_of(int i)
{
	return i < 0 ? "default" : newt_name[i];
}

/* the color a name stands for: the theme's text or background for default */
static const char *
color_of(const TmTheme *t, int i, int text)
{
	return i >= 0 ? t->ansi[i] : text ? t->fg : t->bg;
}

static long
dist(const char *a, const char *b)
{
	long d = 0;

	for (int sh = 16; sh >= 0; sh -= 8) {
		long x = ((strtol(a, NULL, 16) >> sh) & 0xff) - ((strtol(b, NULL, 16) >> sh) & 0xff);

		d += x * x;
	}
	return d;
}

/* 1 when text f on background b looks like one of the pairs in avoid */
static int
looks_like(const TmTheme *t, int f, int b, const int *avoid)
{
	for (; avoid && avoid[0] > -2; avoid += 2)
		if (tm_contrast(color_of(t, f, 1), color_of(t, avoid[0], 1)) < 1.5 &&
		    tm_contrast(color_of(t, b, 0), color_of(t, avoid[1], 0)) < 1.5)
			return 1;
	return 0;
}

/* the pair of names for the roles fg on bg: the background nearest bg that some text
 * reads on at min, and on it the text nearest fg that does (when no pair reads, the
 * pair that reads best), into *pf and *pb. any_bg 0 leaves "default" out of the
 * backgrounds. A pair that looks like one in avoid (text, background, ..., ended by
 * -2), both colors within 1.5:1 of theirs, is not taken: a focused element must not
 * look like an unfocused one; when that leaves no pair that reads, it is taken after
 * all. */
static void
pick(const TmTheme *t, const char *fg, const char *bg, double min, int any_bg, const int *avoid,
     int *pf, int *pb)
{
	int order[17], n = 0, cf = -1, cb = -1;
	double best_c = -1;

	for (int b = any_bg ? -1 : 0; b < 16; b++) {      /* backgrounds, nearest first */
		int k = n++;

		while (k > 0 && dist(color_of(t, order[k - 1], 0), bg) > dist(color_of(t, b, 0), bg)) {
			order[k] = order[k - 1];
			k--;
		}
		order[k] = b;
	}
	for (int i = 0; i < n; i++) {
		int b = order[i], bf = -2;
		const char *bc = color_of(t, b, 0);

		for (int f = -1; f < 16; f++) {
			const char *fc = color_of(t, f, 1);
			double c = tm_contrast(fc, bc);

			if (looks_like(t, f, b, avoid))
				continue;
			if (c > best_c) {
				best_c = c;
				cf = f;
				cb = b;
			}
			if (c >= min && (bf == -2 || dist(fc, fg) < dist(color_of(t, bf, 1), fg)))
				bf = f;
		}
		if (bf != -2) {
			*pf = bf;
			*pb = b;
			return;
		}
	}
	if (avoid) {            /* no pair both reads and stands out: reading comes first */
		pick(t, fg, bg, min, any_bg, NULL, pf, pb);
		return;
	}
	*pf = cf;
	*pb = cb;
}

static void
add(char *buf, size_t n, const char *key, int f, int b)
{
	char tmp[96];

	snprintf(tmp, sizeof tmp, "%s%s=%s,%s", buf[0] ? ":" : "", key, name_of(f), name_of(b));
	strncat(buf, tmp, n - strlen(buf) - 1);
}

/* the module's own variable: the NEWT_COLORS value */
static int
template_vars(const TmTheme *t, TmVar *v, int max)
{
	static char colors[1024];
	int tf, tb, sf, sb, bf, bb, af, ab, f, b;   /* text on bg and on surf, buttons, selection */

	if (max < 1)
		return 0;
	colors[0] = '\0';
	pick(t, t->fg, t->bg, 4.5, 1, NULL, &tf, &tb);
	pick(t, t->fg, t->surf, 4.5, 1, NULL, &sf, &sb);
	/* buttons: newt draws a compact button (whiptail's <Ok>) in compactbutton, and
	 * in button when it has the focus; a full button in button, and in actbutton when
	 * focused. So compactbutton is the window's text, button stands out from it, and
	 * the accent (the selection everywhere) stands out from both. */
	{
		int plain[] = { tf, tb, -2 }, both[5];

		pick(t, t->fg, t->surf, 4.5, 1, plain, &bf, &bb);
		both[0] = tf, both[1] = tb, both[2] = bf, both[3] = bb, both[4] = -2;
		pick(t, t->accfg, t->acc, 4.5, 1, both, &af, &ab);
	}
	add(colors, sizeof colors, "root", tf, tb);
	add(colors, sizeof colors, "window", tf, tb);
	add(colors, sizeof colors, "label", tf, tb);
	add(colors, sizeof colors, "listbox", tf, tb);
	add(colors, sizeof colors, "checkbox", tf, tb);
	add(colors, sizeof colors, "textbox", tf, tb);
	add(colors, sizeof colors, "roottext", tf, tb);
	add(colors, sizeof colors, "button", bf, bb);
	add(colors, sizeof colors, "compactbutton", tf, tb);
	add(colors, sizeof colors, "entry", sf, sb);
	add(colors, sizeof colors, "sellistbox", sf, sb);
	add(colors, sizeof colors, "helpline", sf, sb);
	add(colors, sizeof colors, "actbutton", af, ab);
	add(colors, sizeof colors, "actcheckbox", af, ab);
	add(colors, sizeof colors, "actlistbox", af, ab);
	add(colors, sizeof colors, "actsellistbox", af, ab);
	add(colors, sizeof colors, "acttextbox", af, ab);
	pick(t, t->acc, t->bg, 3.0, 1, NULL, &f, &b);
	add(colors, sizeof colors, "border", f, b);
	pick(t, t->acc, t->bg, 4.5, 1, NULL, &f, &b);
	add(colors, sizeof colors, "title", f, b);
	pick(t, t->ansi[8], t->surf, 3.0, 1, NULL, &f, &b);
	add(colors, sizeof colors, "disentry", f, b);
	/* the scales: newt reads only the background (its text is always white) */
	pick(t, t->fg, t->surf, 1.0, 1, NULL, &f, &b);
	add(colors, sizeof colors, "emptyscale", f, b);
	/* the filled part: the ANSI color nearest the accent that shows on the window and,
	 * when one does, under newt's percentage, which it draws in "white" (color 15) */
	b = -1;
	for (int pass = 0; pass < 2 && b < 0; pass++)
		for (int i = 0; i < 16; i++)
			if (tm_contrast(t->ansi[i], t->bg) >= 1.5 &&
			    (pass || tm_contrast(t->ansi[i], t->ansi[15]) >= 3.0) &&
			    (b < 0 || dist(t->ansi[i], t->acc) < dist(t->ansi[b], t->acc)))
				b = i;
	add(colors, sizeof colors, "fullscale", -1, b < 0 ? 8 : b);
	add(colors, sizeof colors, "shadow", tb, tb);
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

/* the newt library, which nmtui, whiptail and the other dialogs are built on */
static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have_lib("libnewt.so.0.52") || tm_have("nmtui") || tm_have("whiptail");
}

static const TmModule mod = {
	.name = "newt",
	.desc = "newt: the colors of nmtui and other newt dialogs",
	.templates = "newt",
	.default_on = 1,
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
