/* scheme.c: colour schemes in and out. A scheme is a theme file, NAME.theme, with
 * a [dark] and a [light] section (theme.c reads them).
 *
 * Import reads the sixteen terminal colors of a scheme, from:
 *   - a themer .theme file, taken as it is;
 *   - Gogh-style lines, "name= #000000 #aa0000 ... (16 colors)", one scheme per
 *     line, any number of them (the list the shipped themes were made from);
 *   - Xresources (*color0: #...), kitty (color0 #...), foot (regular0=...,
 *     bright0=...), alacritty ([colors.normal] black = "#..."), each with its
 *     background and foreground when it has them.
 * The side the scheme is (dark or light, from its background) is filled from its
 * colors; the other side is derived from them (see derive): the scheme's white as the
 * page, its background as the text, the same accent, every color kept in hue and
 * moved in lightness until it reads. rederive makes a derived side again. The roles come from the sixteen colors: bg (the
 * background, or ansi0), fg (the white that reads best on it), surf (bg a step
 * toward fg), urgent and warning (the red and the yellow that read better),
 * and the accent: the most saturated of the rest that reads 3:1, yellows and
 * oranges weighted down so it is not the warning color. A scheme lands in
 * ~/.config/themer/themes, where the user's own live.
 *
 * Remove deletes a scheme of the user's; a built-in one (from the install) is
 * hidden instead, by its name in ~/.config/themer/themes/.removed, since the
 * install is not the user's to change. Restore takes names off that list. */
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "themer.h"

#define HEX 7                 /* six digits and the NUL */
#define MAX_SCHEMES 512
/* the comment over a derived section; rederive finds them by its start */
#define DERIVED_NOTE(from) "derived from the " from " section: its colors with the light and dark ends swapped, the accent kept"
#define DERIVED_MARK "# derived from the "

typedef struct {
	char name[NAME_LEN];
	char ansi[16][HEX];
	char bg[HEX], fg[HEX];    /* the scheme's own background and foreground, or "" */
	int n;                    /* how many of the sixteen were found */
} Raw;

/* ---- color arithmetic (the readability rules of theme.c use tm_contrast) ---- */

static void
rgb(const char *h, double *r, double *g, double *b)
{
	unsigned v = (unsigned)strtoul(h, NULL, 16);

	*r = (v >> 16 & 255) / 255.0;
	*g = (v >> 8 & 255) / 255.0;
	*b = (v & 255) / 255.0;
}

static void
hex_of(double r, double g, double b, char *out)
{
	int R = (int)lround(fmin(fmax(r, 0), 1) * 255), G = (int)lround(fmin(fmax(g, 0), 1) * 255);
	int B = (int)lround(fmin(fmax(b, 0), 1) * 255);

	snprintf(out, HEX, "%02x%02x%02x", R, G, B);
}

static void
to_hls(const char *c, double *h, double *l, double *s)
{
	double r, g, b, mx, mn, d;

	rgb(c, &r, &g, &b);
	mx = fmax(r, fmax(g, b));
	mn = fmin(r, fmin(g, b));
	*l = (mx + mn) / 2;
	if (mx == mn) {
		*h = *s = 0;
		return;
	}
	d = mx - mn;
	*s = *l <= 0.5 ? d / (mx + mn) : d / (2 - mx - mn);
	if (mx == r)
		*h = (g - b) / d;
	else if (mx == g)
		*h = 2 + (b - r) / d;
	else
		*h = 4 + (r - g) / d;
	*h = fmod(*h / 6 + 1, 1);
}

static double
hue2(double m1, double m2, double h)
{
	h = fmod(h + 1, 1);
	if (h < 1.0 / 6)
		return m1 + (m2 - m1) * h * 6;
	if (h < 0.5)
		return m2;
	if (h < 2.0 / 3)
		return m1 + (m2 - m1) * (2.0 / 3 - h) * 6;
	return m1;
}

static void
from_hls(double h, double l, double s, char *out)
{
	double m2, m1;

	if (s == 0) {
		hex_of(l, l, l, out);
		return;
	}
	m2 = l <= 0.5 ? l * (1 + s) : l + s - l * s;
	m1 = 2 * l - m2;
	hex_of(hue2(m1, m2, h + 1.0 / 3), hue2(m1, m2, h), hue2(m1, m2, h - 1.0 / 3), out);
}

static void
mix(const char *a, const char *b, double t, char *out)
{
	double ar, ag, ab, br, bg, bb;

	rgb(a, &ar, &ag, &ab);
	rgb(b, &br, &bg, &bb);
	hex_of(ar + (br - ar) * t, ag + (bg - ag) * t, ab + (bb - ab) * t, out);
}

static int
is_dark(const char *bg)
{
	return tm_contrast(bg, "ffffff") > tm_contrast(bg, "000000");
}

/* c moved toward the lightness 'toward' until it reads at ratio on bg */
static void
readable(const char *c, const char *bg, double ratio, double toward, char *out)
{
	double h, l, s;
	char t[HEX];

	if (tm_contrast(c, bg) >= ratio) {
		snprintf(out, HEX, "%s", c);
		return;
	}
	to_hls(c, &h, &l, &s);
	for (int i = 1; i <= 100; i++) {
		from_hls(h, l + (toward - l) * i / 100, s, t);
		if (tm_contrast(t, bg) >= ratio) {
			snprintf(out, HEX, "%s", t);
			return;
		}
	}
	from_hls(h, toward, s, out);
}

/* ---- the roles of one side ---- */

typedef struct { char c[C_N][HEX]; } Roles;

static const int acc_cands[] = { 2, 3, 4, 5, 6, 10, 11, 12, 13, 14 };

/* The roles of one side. The accent: with force >= 0 that terminal color (the derived
 * side takes the accent its source side has, so a scheme keeps its color in both
 * modes); otherwise the most saturated of the rest that reads 3:1, yellows and
 * oranges weighted down so it is not the warning color, and *chosen says which. */
static void
roles(char ansi[16][HEX], const char *bg_in, const char *fg_in, Roles *r, int force, int *chosen)
{
	const char *bg = bg_in && bg_in[0] ? bg_in : ansi[0];
	int dark = is_dark(bg), distinct = 0, pick = 1;
	char fg[HEX], urg[HEX], warn[HEX], acc[HEX];
	double best = -2;

	if (fg_in && fg_in[0] && tm_contrast(fg_in, bg) >= 4.5) {
		snprintf(fg, HEX, "%s", fg_in);
	} else {
		snprintf(fg, HEX, "%s", tm_contrast(ansi[7], bg) >= tm_contrast(ansi[15], bg) ? ansi[7] : ansi[15]);
		if (tm_contrast(fg, bg) < 4.5)
			for (int i = 0; i < 16; i++)
				if (tm_contrast(ansi[i], bg) > tm_contrast(fg, bg))
					snprintf(fg, HEX, "%s", ansi[i]);
	}
	snprintf(r->c[C_BG], HEX, "%s", bg);
	snprintf(r->c[C_FG], HEX, "%s", fg);
	mix(bg, fg, dark ? 0.12 : 0.10, r->c[C_SURF]);
	snprintf(urg, HEX, "%s", tm_contrast(ansi[1], bg) >= tm_contrast(ansi[9], bg) ? ansi[1] : ansi[9]);
	snprintf(warn, HEX, "%s", tm_contrast(ansi[3], bg) >= tm_contrast(ansi[11], bg) ? ansi[3] : ansi[11]);
	readable(urg, bg, 3.0, dark ? 0.85 : 0.1, r->c[C_URG]);
	readable(warn, bg, 3.0, dark ? 0.85 : 0.1, r->c[C_WARN]);
	if (force >= 0 && force < 16) {
		pick = force;
	} else {
		/* monochrome schemes take their one color */
		for (int i = 1; i < 7; i++) {
			int seen = 0;

			for (int k = 1; k < i; k++)
				seen |= !strcmp(ansi[i], ansi[k]);
			distinct += !seen;
		}
		if (distinct > 2)
			for (size_t k = 0; k < sizeof acc_cands / sizeof acc_cands[0]; k++) {
				const char *c = ansi[acc_cands[k]];
				double h, l, sat, cr = tm_contrast(c, bg), score;

				to_hls(c, &h, &l, &sat);
				if (cr < 2.0 || !strcmp(c, urg) || !strcmp(c, warn))
					score = -1;
				else    /* yellows and oranges are the warning color's */
					score = sat * (h > 0.06 && h < 0.2 ? 0.5 : 1.0) * fmin(cr, 4.5) / 4.5;
				if (score > best) {
					best = score;
					pick = acc_cands[k];
				}
			}
	}
	if (chosen)
		*chosen = pick;
	snprintf(acc, HEX, "%s", ansi[pick]);
	readable(acc, bg, 3.0, dark ? 0.9 : 0.1, r->c[C_ACC]);
	snprintf(r->c[C_ACCFG], HEX, "%s",
	         tm_contrast(r->c[C_ACC], "000000") >= tm_contrast(r->c[C_ACC], "ffffff") ? "000000" : "ffffff");
}

static double
clampd(double v, double lo, double hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

/* The other side of a scheme, made from the scheme's own colors as an author would
 * make it:
 *  - the new background is the scheme's own color from the other end: for a light
 *    side its white (the foreground or bright white the author chose: Gruvbox's
 *    cream, Nord's snow), for a dark side its darkest neutral. Its hue and
 *    saturation are kept; a white with no tint of its own takes the background's
 *    tint instead, and where neither has one, a soft tint of the scheme's accent.
 *    Its lightness, between paper (86%) and near-white (95%), half from how light the
 *    white is and half from how dark the source background is: a soft scheme with a
 *    grey white gets soft paper, a near-black one with pure white gets near-white
 *    (a derived dark side the same way round, 6% to 20%);
 *  - the new text is the source background, made dark (or light) enough;
 *  - every terminal color keeps its hue and saturation and moves in lightness only,
 *    as far as it must to read 4.5:1 (by luminance, so a green goes as dark as a
 *    green must, and stays green). */
static void
derive(char in[16][HEX], const char *bg_in, const char *fg_in, int to_dark, int accent, char out[16][HEX])
{
	const char *bg = bg_in && bg_in[0] ? bg_in : in[0];
	const char *fg = fg_in && fg_in[0] ? fg_in
	               : tm_contrast(in[7], bg) >= tm_contrast(in[15], bg) ? in[7] : in[15];
	const char *cands[4] = { fg, in[15], in[7], NULL };
	const char *src = fg;
	char nbg[HEX], nfg[HEX];
	double bh, bl, bs, h, l, sat;

	if (to_dark) {
		cands[1] = in[0];
		cands[2] = in[8];
	}
	/* the white (or the black) of the scheme: the lightest (darkest) of its neutrals */
	for (int i = 0; cands[i]; i++) {
		double ch, cl, cs, sl;

		to_hls(cands[i], &ch, &cl, &cs);
		to_hls(src, &h, &sl, &sat);
		if (to_dark ? cl < sl : cl > sl)
			src = cands[i];
	}
	to_hls(bg, &bh, &bl, &bs);
	to_hls(src, &h, &l, &sat);
	if (sat < 0.1 || l > 0.985 || l < 0.015) {     /* untinted: the background's tint */
		h = bh;
		sat = fmin(bs * 0.8, 0.4);
	}
	if (sat < 0.08 && accent >= 0) {        /* neither has one: the accent's, softly */
		double al, as;

		to_hls(in[accent], &h, &al, &as);
		sat = fmin(as, 1.0) * 0.3;
	}
	sat = fmin(sat, 0.6);
	/* the white's (black's) own lightness, within the range real light (dark)
	 * themes span: from paper (86%) to near-white (95%) */
	if (to_dark) {
		double tl = clampd((0.28 - l) / 0.26, 0, 1), tb = clampd((bl - 0.78) / 0.19, 0, 1);

		l = 0.2 - 0.14 * (tl + tb) / 2;
	} else {
		double tl = clampd((l - 0.72) / 0.26, 0, 1), tb = clampd((0.22 - bl) / 0.19, 0, 1);

		l = 0.86 + 0.09 * (tl + tb) / 2;
	}
	from_hls(h, l, sat, nbg);
	/* the text: the source background, as dark (light) as it must be */
	to_hls(bg, &h, &l, &sat);
	{
		char t[HEX];

		from_hls(h, to_dark ? clampd(l, 0.8, 0.92) : clampd(l, 0.08, 0.22), fmin(sat, 0.5), t);
		readable(t, nbg, 7.0, to_dark ? 0.97 : 0.03, nfg);
	}
	for (int i = 0; i < 16; i++) {
		if (i == 0)
			snprintf(out[i], HEX, "%s", nbg);
		else if (i == 15)
			snprintf(out[i], HEX, "%s", nfg);
		else if (i == 7 || i == 8) {    /* the greys: the text toward the page, still reading */
			char t[HEX];

			mix(nfg, nbg, i == 7 ? 0.25 : 0.5, t);
			readable(t, nbg, 4.5, to_dark ? 0.95 : 0.05, out[i]);
		} else
			readable(in[i], nbg, 4.5, to_dark ? 0.95 : 0.05, out[i]);
	}
}

static void
write_section(FILE *f, const char *name, char ansi[16][HEX], const char *bg, const char *fg,
              const char *note, int force, int *chosen)
{
	Roles r;

	roles(ansi, bg, fg, &r, force, chosen);
	if (note)
		fprintf(f, "# %s\n", note);
	fprintf(f, "[%s]\n", name);
	for (int i = 0; i < C_N; i++)
		fprintf(f, "%s=%s\n", color_names[i], r.c[i]);
	for (int i = 0; i < 16; i++)
		fprintf(f, "ansi%d=%s\n", i, ansi[i]);
}

/* ---- where schemes live ---- */

static int
user_dir(char *out, size_t n)
{
	return path_under_home(out, n, ".config/themer/themes");
}

/* adds name to .removed (hide) or takes it off (show), or with name NULL and
 * hide 0 empties the list */
static int
set_removed(const char *name, int hide)
{
	char p[PATH_MAX], line[NAME_LEN + 2], *names = NULL;
	size_t len = 0;
	FILE *f, *m = open_memstream(&names, &len);
	AFile a;
	int r;

	if (!m)
		return -1;
	if (user_dir(p, sizeof p) < 0 || strlen(p) + 10 >= sizeof p) {
		fclose(m);
		free(names);
		return -1;
	}
	strcat(p, "/.removed");
	if (name && (f = fopen(p, "r"))) {
		while (fgets(line, sizeof line, f)) {
			line[strcspn(line, "\r\n")] = '\0';
			if (line[0] && strcmp(line, name))
				fprintf(m, "%s\n", line);
		}
		fclose(f);
	}
	if (name && hide)
		fprintf(m, "%s\n", name);
	fclose(m);
	if (!len) {
		free(names);
		return unlink(p) < 0 && access(p, F_OK) == 0 ? -1 : 0;
	}
	if (!(f = afile_open(&a, p, 0))) {
		free(names);
		return -1;
	}
	fputs(names, f);
	free(names);
	r = afile_commit(&a);
	return r;
}

/* ---- reading a file ---- */

/* a lowercase name of letters, digits and '-' from s */
static void
clean_name(const char *s, char *out, size_t n)
{
	size_t o = 0;

	for (; *s && o + 1 < n; s++) {
		unsigned char c = (unsigned char)*s;

		if (isalnum(c))
			out[o++] = (char)tolower(c);
		else if ((c == '_' || c == '-' || c == ' ' || c == '.') && o && out[o - 1] != '-')
			out[o++] = '-';
	}
	while (o && out[o - 1] == '-')
		o--;
	out[o] = '\0';
}

/* the first six-digit hex color in s (with or without '#'), or NULL */
static const char *
find_hex(const char *s, char *out)
{
	for (const char *p = s; *p; p++) {
		int k = 0;

		while (k < 6 && isxdigit((unsigned char)p[k]))
			k++;
		if (k == 6 && !isxdigit((unsigned char)p[6]) && (p == s || !isalnum((unsigned char)p[-1]))) {
			for (int i = 0; i < 6; i++)
				out[i] = (char)tolower((unsigned char)p[i]);
			out[6] = '\0';
			return p;
		}
	}
	return NULL;
}

static const char *const ansi_names[8] = {
	"black", "red", "green", "yellow", "blue", "magenta", "cyan", "white"
};

/* Reads every scheme in a file into raw (at most max); returns how many. */
static int
read_schemes(const char *path, Raw *raw, int max)
{
	char *line = NULL, section[64] = "";
	size_t cap = 0;
	FILE *f = fopen(path, "r");
	Raw one;
	int n = 0;

	if (!f)
		return -1;
	memset(&one, 0, sizeof one);
	while (getline(&line, &cap, f) > 0) {
		char hex[HEX], key[64];
		const char *at, *s = line;
		size_t kl;
		int idx = -1;

		while (*s == ' ' || *s == '\t')
			s++;
		if (*s == '[') {
			snprintf(section, sizeof section, "%.*s", (int)strcspn(s + 1, "]\r\n"), s + 1);
			continue;
		}
		if (*s == '!' || (*s == '#' && !isxdigit((unsigned char)s[1])) || *s == ';')
			continue;
		/* Gogh: name= then sixteen colors on one line */
		{
			const char *eq = strchr(s, '=');
			char *p;
			int k = 0;
			Raw g;

			memset(&g, 0, sizeof g);
			if (eq && !strncmp(s, "COLORS='", 8))
				s += 8;
			eq = strchr(s, '=');
			if (eq) {
				p = (char *)eq + 1;
				while (k < 16 && (at = find_hex(p, hex))) {
					snprintf(g.ansi[k++], HEX, "%s", hex);
					p = (char *)at + 6;
				}
				if (k == 16 && n < max) {
					char nm[NAME_LEN];

					snprintf(nm, sizeof nm, "%.*s", (int)(eq - s), s);
					clean_name(nm, g.name, sizeof g.name);
					g.n = 16;
					raw[n++] = g;
					continue;
				}
			}
		}
		/* one color per line: the key is what comes before the color */
		if (!(at = find_hex(s, hex)))
			continue;
		kl = (size_t)(at - s);
		snprintf(key, sizeof key, "%.*s", (int)(kl < sizeof key ? kl : sizeof key - 1), s);
		for (char *k = key; *k; k++)
			*k = (char)tolower((unsigned char)*k);
		if (strstr(key, "background") && !strstr(key, "selection"))
			snprintf(one.bg, HEX, "%s", hex);
		else if (strstr(key, "foreground") && !strstr(key, "selection") && !strstr(key, "cursor"))
			snprintf(one.fg, HEX, "%s", hex);
		else {
			const char *c;

			/* color12, regular3, bright3 */
			if ((c = strstr(key, "color")) && isdigit((unsigned char)c[5]))
				idx = atoi(c + 5);
			else if ((c = strstr(key, "regular")) && isdigit((unsigned char)c[7]))
				idx = atoi(c + 7);
			else if ((c = strstr(key, "bright")) && isdigit((unsigned char)c[6]))
				idx = 8 + atoi(c + 6);
			else    /* alacritty: black = "#..." under [colors.normal] or [colors.bright] */
				for (int i = 0; i < 8; i++)
					if (!strncmp(key, ansi_names[i], strlen(ansi_names[i])) &&
					    (strstr(section, "normal") || strstr(section, "bright")))
						idx = i + (strstr(section, "bright") ? 8 : 0);
			if (idx >= 0 && idx < 16) {
				if (!one.ansi[idx][0])
					one.n++;
				snprintf(one.ansi[idx], HEX, "%s", hex);
			}
		}
	}
	free(line);
	fclose(f);
	if (one.n == 16 && n < max)
		raw[n++] = one;
	return n;
}

/* writes one scheme as NAME.theme in the user's directory */
static int
write_scheme(const Raw *r, const char *source, FILE *report)
{
	char dir[PATH_MAX], path[PATH_MAX], other[16][HEX], ansi[16][HEX];
	const char *bg = r->bg[0] ? r->bg : r->ansi[0];
	int dark = is_dark(bg);
	AFile a;
	FILE *f;

	if (!valid_name(r->name) || user_dir(dir, sizeof dir) < 0 ||
	    snprintf(path, sizeof path, "%s/%s.theme", dir, r->name) >= (int)sizeof path)
		return -1;
	memcpy(ansi, r->ansi, sizeof ansi);
	if (!(f = afile_open(&a, path, 0)))
		return -1;
	fprintf(f, "# %s: %s from %s; the %s section is derived.\n# Imported by themer scheme import.\n",
	        r->name, dark ? "dark" : "light", source, dark ? "light" : "dark");
	{
		Roles src;
		int acc = -1;
		char tmp[16][HEX];

		memcpy(tmp, ansi, sizeof tmp);
		roles(tmp, r->bg, r->fg, &src, -1, &acc);
		derive(ansi, r->bg, r->fg, !dark, acc, other);
		if (dark) {
			write_section(f, "dark", ansi, r->bg, r->fg, NULL, acc, NULL);
			write_section(f, "light", other, NULL, NULL, DERIVED_NOTE("dark"), acc, NULL);
		} else {
			write_section(f, "dark", other, NULL, NULL, DERIVED_NOTE("light"), acc, NULL);
			write_section(f, "light", ansi, r->bg, r->fg, NULL, acc, NULL);
		}
	}
	if (afile_commit(&a) < 0)
		return -1;
	set_removed(r->name, 0);
	if (report)
		fprintf(report, "%s: imported (%s, the %s side derived)\n", r->name,
		        dark ? "dark" : "light", dark ? "light" : "dark");
	return 0;
}

int
scheme_import(const char *path, const char *name, FILE *report)
{
	static Raw raw[MAX_SCHEMES];
	char stem[NAME_LEN], dir[PATH_MAX], dst[PATH_MAX], *line = NULL;
	const char *base = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
	size_t cap = 0;
	int n, done = 0, is_theme = 0;
	FILE *f;

	snprintf(stem, sizeof stem, "%.*s", (int)strcspn(base, "."), base);
	clean_name(name && name[0] ? name : stem, stem, sizeof stem);
	/* a themer theme file: taken as it is, once it reads */
	if (!(f = fopen(path, "r"))) {
		fprintf(report, "%s: cannot read it\n", path);
		return -1;
	}
	while (!is_theme && getline(&line, &cap, f) > 0)
		is_theme = !strncmp(line, "[dark]", 6) || !strncmp(line, "[light]", 7);
	free(line);
	fclose(f);
	if (is_theme) {
		Theme t;
		char *data = NULL;
		size_t len = 0;
		FILE *in;

		if (!valid_name(stem) || user_dir(dir, sizeof dir) < 0 ||
		    snprintf(dst, sizeof dst, "%s/%s.theme", dir, stem) >= (int)sizeof dst)
			return -1;
		if (!(in = fopen(path, "rb")))
			return -1;
		{
			char buf[8192];
			size_t k;
			FILE *m = open_memstream(&data, &len);

			while (m && (k = fread(buf, 1, sizeof buf, in)) > 0)
				fwrite(buf, 1, k, m);
			if (m)
				fclose(m);
		}
		fclose(in);
		if (!data || write_bytes(dst, data, len) < 0) {
			free(data);
			return -1;
		}
		free(data);
		if (theme_read(stem, &t) < 0) {
			unlink(dst);
			fprintf(report, "%s: not a readable theme (both sections need bg, fg, ... and ansi0-15)\n", path);
			return -1;
		}
		set_removed(stem, 0);
		fprintf(report, "%s: imported as it is\n", stem);
		return 1;
	}
	n = read_schemes(path, raw, MAX_SCHEMES);
	if (n <= 0) {
		fprintf(report, "%s: no scheme found (a .theme file, Gogh lines, Xresources, kitty, foot "
		        "or alacritty colors, with all sixteen)\n", path);
		return -1;
	}
	for (int i = 0; i < n; i++) {
		if (n == 1)
			snprintf(raw[i].name, sizeof raw[i].name, "%.*s", (int)sizeof raw[i].name - 1, stem);
		else if (!raw[i].name[0])
			snprintf(raw[i].name, sizeof raw[i].name, "%.*s-%d", (int)sizeof raw[i].name - 8, stem, i + 1);
		if (write_scheme(&raw[i], base, report) == 0)
			done++;
		else
			fprintf(report, "%s: could not write it\n", raw[i].name);
	}
	return done ? done : -1;
}

/* 0: the user's own was deleted; 1: a built-in one is hidden; -1: none */
int
scheme_remove(const char *name)
{
	char dir[PATH_MAX], p[PATH_MAX];
	Theme t;

	if (!valid_name(name) || user_dir(dir, sizeof dir) < 0 ||
	    snprintf(p, sizeof p, "%s/%s.theme", dir, name) >= (int)sizeof p)
		return -1;
	if (!access(p, F_OK))
		return unlink(p);       /* the user's own: gone (a built-in of that name shows again) */
	if (theme_read(name, &t) < 0)
		return -1;              /* no such scheme */
	return set_removed(name, 1) < 0 ? -1 : 1;
}

int
scheme_restore(const char *name)
{
	if (name && !valid_name(name))
		return -1;
	return set_removed(name, 0);
}

/* ---- rederive: a derived side made again from its source side ---- */

/* Makes the derived section of a scheme file again from the section it was derived
 * from, which is kept as it is (with its accent: the derived side takes the same
 * terminal color). A file without a derived section is left alone. 1 when it was
 * rewritten, 0 when there was nothing to do, -1 on error. */
static int
rederive_file(const char *path, FILE *report)
{
	FILE *f = fopen(path, "r");
	char *line = NULL, head[4096] = "", srcsec[8192] = "", ansi[16][HEX] = { { 0 } }, other[16][HEX];
	char bg[HEX] = "", fg[HEX] = "", acc[HEX] = "", from[8] = "";
	size_t cap = 0, hl = 0, sl = 0;
	int where = 0;                  /* 0 head, 1 the source section, 2 the derived one */
	int srcdark = -1, force = -1, got = 0;
	AFile a;
	FILE *o;

	if (!f)
		return -1;
	/* where: 0 the head (comments before any section), 1 the source section,
	 * 2 the derived section (from its marker comment to the next section), 3 after */
	while (getline(&line, &cap, f) > 0) {
		if (!strncmp(line, DERIVED_MARK, strlen(DERIVED_MARK))) {
			snprintf(from, sizeof from, "%s", !strncmp(line + strlen(DERIVED_MARK), "light", 5) ? "light" : "dark");
			where = 2;
			got = 0;
			continue;
		}
		if (line[0] == '[') {
			if (where == 2 && !got) {
				got = 1;        /* the derived section's own header */
				continue;
			}
			where = 1;
			srcdark = !strncmp(line, "[dark]", 6);
		}
		if (where == 2)
			continue;       /* the derived section's lines */
		if (where == 0) {
			if (hl + strlen(line) < sizeof head)
				hl += (size_t)snprintf(head + hl, sizeof head - hl, "%s", line);
			continue;
		}
		{
			char k[16], v[16];

			if (sl + strlen(line) < sizeof srcsec)
				sl += (size_t)snprintf(srcsec + sl, sizeof srcsec - sl, "%s", line);
			if (sscanf(line, "%15[^=]=%15s", k, v) == 2 && strlen(v) == 6) {
				if (!strncmp(k, "ansi", 4) && atoi(k + 4) >= 0 && atoi(k + 4) < 16)
					snprintf(ansi[atoi(k + 4)], HEX, "%.6s", v);
				else if (!strcmp(k, "bg"))
					snprintf(bg, HEX, "%.6s", v);
				else if (!strcmp(k, "fg"))
					snprintf(fg, HEX, "%.6s", v);
				else if (!strcmp(k, "acc"))
					snprintf(acc, HEX, "%.6s", v);
			}
		}
	}
	free(line);
	fclose(f);
	if (!from[0])
		return 0;
	if (srcdark < 0 || !bg[0] || !acc[0])
		return -1;
	for (int i = 0; i < 16; i++)
		if (!ansi[i][0])
			return -1;
	/* the source side's accent: the terminal color it was made from */
	{
		double best = 9, ah, al, as;
		int dk = is_dark(bg);

		to_hls(acc, &ah, &al, &as);
		for (int i = 1; i < 16 && force < 0; i++) {
			char t[HEX];

			readable(ansi[i], bg, 3.0, dk ? 0.9 : 0.1, t);
			if (!strcmp(t, acc) || !strcmp(ansi[i], acc))
				force = i;
		}
		if (force < 0)
			for (int i = 1; i < 16; i++) {
				double h, l, s, d;

				if (i == 7 || i == 8 || i == 15)
					continue;
				to_hls(ansi[i], &h, &l, &s);
				d = fabs(h - ah);
				d = fmin(d, 1 - d) + fabs(s - as) * 0.2;
				if (d < best) {
					best = d;
					force = i;
				}
			}
	}
	derive(ansi, bg, fg, !srcdark, force, other);
	if (!(o = afile_open(&a, path, 0)))
		return -1;
	fputs(head, o);
	if (srcdark) {
		fputs(srcsec, o);
		write_section(o, "light", other, NULL, NULL, DERIVED_NOTE("dark"), force, NULL);
	} else {
		write_section(o, "dark", other, NULL, NULL, DERIVED_NOTE("light"), force, NULL);
		fputs(srcsec, o);
	}
	if (afile_commit(&a) < 0)
		return -1;
	if (report)
		fprintf(report, "%s: the %s side made again\n", path, srcdark ? "light" : "dark");
	return 1;
}

int
scheme_rederive(int n, char **paths, FILE *report)
{
	char dir[PATH_MAX], p[PATH_MAX + 300];
	int done = 0, bad = 0;

	if (n > 0) {
		for (int i = 0; i < n; i++) {
			int r = rederive_file(paths[i], report);

			bad += r < 0;
			done += r > 0;
			if (r < 0 && report)
				fprintf(report, "%s: not a scheme file it can read\n", paths[i]);
		}
	} else if (user_dir(dir, sizeof dir) == 0) {
		DIR *d = opendir(dir);
		struct dirent *e;

		while (d && (e = readdir(d))) {
			size_t l = strlen(e->d_name);

			if (l < 7 || strcmp(e->d_name + l - 6, ".theme"))
				continue;
			snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
			{
				int r = rederive_file(p, report);

				bad += r < 0;
				done += r > 0;
			}
		}
		if (d)
			closedir(d);
	}
	if (report)
		fprintf(report, "%d scheme%s made again%s\n", done, done == 1 ? "" : "s", bad ? ", some could not be read" : "");
	return bad ? -1 : done;
}
