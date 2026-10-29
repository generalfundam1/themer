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
 * colors; the other side is derived: the same hues, the lightness flipped, each
 * color moved until it reads. The roles come from the sixteen colors: bg (the
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
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "themer.h"

#define HEX 7                 /* six digits and the NUL */
#define MAX_SCHEMES 512

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
	for (int i = 1; i <= 40; i++) {
		from_hls(h, l + (toward - l) * i / 40, s, t);
		if (tm_contrast(t, bg) >= ratio) {
			snprintf(out, HEX, "%s", t);
			return;
		}
	}
	from_hls(h, toward, s, out);
}

/* ---- the roles of one side ---- */

typedef struct { char c[C_N][HEX]; } Roles;

static void
roles(char ansi[16][HEX], const char *bg_in, const char *fg_in, Roles *r)
{
	static const int cands[] = { 2, 3, 4, 5, 6, 10, 11, 12, 13, 14 };
	const char *bg = bg_in && bg_in[0] ? bg_in : ansi[0];
	int dark = is_dark(bg), distinct = 0;
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
	readable(urg, bg, 3.0, dark ? 0.7 : 0.35, r->c[C_URG]);
	readable(warn, bg, 3.0, dark ? 0.7 : 0.35, r->c[C_WARN]);
	/* the accent: monochrome schemes take their one color */
	for (int i = 1; i < 7; i++) {
		int seen = 0;

		for (int k = 1; k < i; k++)
			seen |= !strcmp(ansi[i], ansi[k]);
		distinct += !seen;
	}
	snprintf(acc, HEX, "%s", ansi[1]);
	if (distinct > 2)
		for (size_t k = 0; k < sizeof cands / sizeof cands[0]; k++) {
			const char *c = ansi[cands[k]];
			double h, l, s, cr = tm_contrast(c, bg), score;

			to_hls(c, &h, &l, &s);
			if (cr < 2.0 || !strcmp(c, urg) || !strcmp(c, warn))
				score = -1;
			else    /* yellows and oranges are the warning color's */
				score = s * (h > 0.06 && h < 0.2 ? 0.5 : 1.0) * fmin(cr, 4.5) / 4.5;
			if (score > best) {
				best = score;
				snprintf(acc, HEX, "%s", c);
			}
		}
	readable(acc, bg, 3.0, dark ? 0.75 : 0.3, r->c[C_ACC]);
	snprintf(r->c[C_ACCFG], HEX, "%s",
	         tm_contrast(r->c[C_ACC], "000000") >= tm_contrast(r->c[C_ACC], "ffffff") ? "000000" : "ffffff");
}

/* the other side of a scheme: the same hues, the lightness flipped */
static void
derive(char in[16][HEX], const char *bg_in, const char *fg_in, int to_dark, char out[16][HEX])
{
	const char *bg = bg_in && bg_in[0] ? bg_in : in[0];
	const char *fg = fg_in && fg_in[0] ? fg_in
	               : tm_contrast(in[7], bg) >= tm_contrast(in[15], bg) ? in[7] : in[15];
	char nbg[HEX], nfg[HEX];
	double h, l, s;

	to_hls(bg, &h, &l, &s);
	from_hls(h, to_dark ? 0.13 : 0.94, fmin(s, 0.25), nbg);
	to_hls(fg, &h, &l, &s);
	from_hls(h, to_dark ? 0.85 : 0.18, fmin(s, 0.3), nfg);
	for (int i = 0; i < 16; i++) {
		if (i == 0)
			snprintf(out[i], HEX, "%s", nbg);
		else if (i == 15)
			snprintf(out[i], HEX, "%s", nfg);
		else if (i == 7)
			mix(nfg, nbg, 0.25, out[i]);
		else if (i == 8)
			mix(nfg, nbg, 0.55, out[i]);
		else
			readable(in[i], nbg, 4.5, to_dark ? 0.7 : 0.35, out[i]);
	}
}

static void
write_section(FILE *f, const char *name, char ansi[16][HEX], const char *bg, const char *fg,
              const char *note)
{
	Roles r;

	roles(ansi, bg, fg, &r);
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
	derive(ansi, r->bg, r->fg, !dark, other);
	if (!(f = afile_open(&a, path, 0)))
		return -1;
	fprintf(f, "# %s: %s from %s; the %s section is derived.\n# Imported by themer scheme import.\n",
	        r->name, dark ? "dark" : "light", source, dark ? "light" : "dark");
	if (dark) {
		write_section(f, "dark", ansi, r->bg, r->fg, NULL);
		write_section(f, "light", other, NULL, NULL, "derived from the dark section: same hues, lightness flipped");
	} else {
		write_section(f, "dark", other, NULL, NULL, "derived from the light section: same hues, lightness flipped");
		write_section(f, "light", ansi, r->bg, r->fg, NULL);
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
		if (n == 1 || !raw[i].name[0])
			snprintf(raw[i].name, sizeof raw[i].name, "%s", n == 1 ? stem : raw[i].name);
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
