/* theme.c: theme files, the readability rules, and the effective colors.
 * A theme file is key=value under [dark] and [light] sections, '#' comments.
 * Lookup order: $THEMER_THEMES, ~/.config/themer/themes, SYS_THEMES_DIR. */
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "themer.h"

/* the keys of a theme file, in C_* order */
const char *color_names[C_N] = { "bg", "fg", "surf", "acc", "accfg", "urg", "warn" };

static int
find_theme(const char *name, char *out, size_t n)
{
	const char *env = getenv("THEMER_THEMES");
	char dir[PATH_MAX];

	if (env) {
		snprintf(out, n, "%s/%s.theme", env, name);
		if (!access(out, R_OK))
			return 0;
	}
	if (path_under_home(dir, sizeof dir, ".config/themer/themes") == 0 &&
	    snprintf(out, n, "%s/%s.theme", dir, name) < (int)n) {
		if (!access(out, R_OK))
			return 0;
	}
	snprintf(out, n, "%s/%s.theme", SYS_THEMES_DIR, name);
	return access(out, R_OK) ? -1 : 0;
}

static int
parse_hex(const char *v, char *out)
{
	const char *h = v[0] == '#' ? v + 1 : v;

	if (strlen(h) != 6)
		return -1;
	for (const char *p = h; *p; p++)
		if (!isxdigit((unsigned char)*p))
			return -1;
	snprintf(out, TM_COLOR_LEN, "%s", h);
	return 0;
}

int
theme_read(const char *name, Theme *t)
{
	char path[PATH_MAX], *line = NULL;
	size_t cap = 0;
	Theme nt;
	FILE *f;
	int sec = -1, ok = 1, s, i;

	if (!valid_name(name) || find_theme(name, path, sizeof path) < 0)
		return -1;
	if (!(f = fopen(path, "r")))
		return -1;
	memset(&nt, 0, sizeof nt);
	snprintf(nt.name, sizeof nt.name, "%s", name);
	while (getline(&line, &cap, f) > 0) {
		char *eq;

		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] == '#' || !line[0])
			continue;
		if (line[0] == '[') {
			sec = !strcmp(line, "[dark]") ? MODE_DARK
			    : !strcmp(line, "[light]") ? MODE_LIGHT : -1;
			continue;
		}
		eq = strchr(line, '=');
		if (!eq || sec < 0)
			continue;
		*eq = '\0';
		for (i = 0; i < C_N; i++)
			if (!strcmp(line, color_names[i]) && parse_hex(eq + 1, nt.hex[sec][i]) < 0)
				ok = 0;
		if (!strncmp(line, "ansi", 4)) {
			int k = atoi(line + 4);

			if (k < 0 || k > 15 || parse_hex(eq + 1, nt.ansi[sec][k]) < 0)
				ok = 0;
		}
	}
	free(line);
	fclose(f);
	if (!ok)
		return -1;
	for (s = 0; s < 2; s++) {
		for (i = 0; i < C_N; i++)
			if (!nt.hex[s][i][0])
				return -1;
		for (i = 0; i < 16; i++)
			if (!nt.ansi[s][i][0])
				return -1;
	}
	*t = nt;
	return 0;
}

/* Sets *out to pref if it reads at text level on every background, otherwise
 * to whichever of pref, black, white is best on the worst of them. Returns 1 if changed. */
static int
text_for(char *out, size_t n, const char *pref, const char *const *bgs, int nb)
{
	char keep[TM_COLOR_LEN];
	const char *cands[3] = { keep, "000000", "ffffff" };
	double best = -1;
	int i, j;

	snprintf(keep, sizeof keep, "%s", pref);   /* pref may alias out */

	for (i = 0; i < 3; i++) {
		double worst = 99;

		for (j = 0; j < nb; j++) {
			double c = tm_contrast(cands[i], bgs[j]);

			if (c < worst)
				worst = c;
		}
		if (worst >= 4.5) {
			int changed = strcmp(out, cands[i]) != 0;

			snprintf(out, n, "%s", cands[i]);
			return changed;
		}
		if (worst > best) {
			best = worst;
			snprintf(out, n, "%s", cands[i]);
		}
	}
	return 1;
}

/* Readability rules, applied to each mode of a theme:
 *   fg       reads at 4.5 on bg and on surf
 *   accfg    reads at 4.5 on acc (selection and focused tab)
 *   urg,warn read at 4.5 on bg (they are also used as text)
 *   ansi8    reads at 4.5 on bg (dim text)
 *   ansi1-15 read at 4.5 on bg (terminal and editor text)
 * Text on the urgent and warning backgrounds is chosen separately, per color,
 * in theme_scheme, because one text color cannot read on all three.
 * A color that fails is moved toward the theme's own text color. Nothing is
 * swapped for an unrelated color, except fg and accfg, which may become black
 * or white when no move toward the text color works. */
static int
enforce_mode(Theme *t, int m)
{
	const char *bg = t->hex[m][C_BG], *fg = t->hex[m][C_FG];
	const char *fg_bgs[2] = { t->hex[m][C_BG], t->hex[m][C_SURF] };
	const char *acc_bgs[1] = { t->hex[m][C_ACC] };
	char tmp[TM_COLOR_LEN];
	int i, changed = 0;

	/* urgent and warning colors first, as text on the bar background */
	for (i = C_URG; i <= C_WARN; i++) {
		if (tm_ensure_contrast(tmp, sizeof tmp, t->hex[m][i], bg, fg, 4.5)) {
			snprintf(t->hex[m][i], TM_COLOR_LEN, "%s", tmp);
			changed = 1;
		}
	}
	changed |= text_for(t->hex[m][C_FG], TM_COLOR_LEN, t->hex[m][C_FG], fg_bgs, 2);
	changed |= text_for(t->hex[m][C_ACCFG], TM_COLOR_LEN, t->hex[m][C_ACCFG], acc_bgs, 1);
	bg = t->hex[m][C_BG];
	fg = t->hex[m][C_FG];
	if (tm_ensure_contrast(tmp, sizeof tmp, t->ansi[m][8], bg, fg, 4.5)) {
		snprintf(t->ansi[m][8], TM_COLOR_LEN, "%s", tmp);
		changed = 1;
	}
	for (i = 1; i < 16; i++) {
		if (i == 8)
			continue;
		if (tm_ensure_contrast(tmp, sizeof tmp, t->ansi[m][i], bg, fg, 4.5)) {
			snprintf(t->ansi[m][i], TM_COLOR_LEN, "%s", tmp);
			changed = 1;
		}
	}
	return changed;
}

/* returns the number of colors moved, over both modes */
int
theme_enforce(Theme *t, int *adjusted)
{
	int m, n = 0;

	for (m = 0; m < 2; m++)
		n += enforce_mode(t, m);
	if (adjusted)
		*adjusted = n;
	return n;
}

/* the theme as the rest of themer uses it: parsed, then made readable */
int
theme_load(const char *name, Theme *t)
{
	Theme nt;

	if (theme_read(name, &nt) < 0)
		return -1;
	theme_enforce(&nt, NULL);
	*t = nt;
	return 0;
}

static void
scan_dir(const char *dir, char names[][NAME_LEN], int *n, int max)
{
	DIR *d = opendir(dir);
	struct dirent *e;

	if (!d)
		return;
	while ((e = readdir(d)) && *n < max) {
		size_t l = strlen(e->d_name);

		if (l < 7 || l - 6 >= NAME_LEN || strcmp(e->d_name + l - 6, ".theme"))
			continue;
		memcpy(names[*n], e->d_name, l - 6);
		names[*n][l - 6] = '\0';
		if (valid_name(names[*n]))
			(*n)++;
	}
	closedir(d);
}

static int
cmp_name(const void *a, const void *b)
{
	return strcmp(a, b);
}

/* Every scheme, sorted, each once: $THEMER_THEMES, the user's, the install's,
 * less those the user removed (themes/.removed, read once here). A removed scheme
 * still loads, so the theme in use and the default keep working. */
int
theme_list(char names[][NAME_LEN], int max)
{
	static char found[2048][NAME_LEN], removed[512][NAME_LEN];
	const char *env = getenv("THEMER_THEMES");
	char dir[PATH_MAX], line[NAME_LEN + 2];
	int nf = 0, nr = 0, n = 0;
	FILE *f;

	if (env)
		scan_dir(env, found, &nf, 2048);
	if (path_under_home(dir, sizeof dir, ".config/themer/themes") == 0) {
		scan_dir(dir, found, &nf, 2048);
		if (strlen(dir) + 10 < sizeof dir && (strcat(dir, "/.removed"), f = fopen(dir, "r"))) {
			while (nr < 512 && fgets(line, sizeof line, f))
				if ((line[strcspn(line, "\r\n")] = '\0', line[0]))
					snprintf(removed[nr++], NAME_LEN, "%.63s", line);
			fclose(f);
		}
	}
	scan_dir(SYS_THEMES_DIR, found, &nf, 2048);
	qsort(found, nf, NAME_LEN, cmp_name);
	for (int i = 0; i < nf && n < max; i++) {
		int skip = i && !strcmp(found[i], found[i - 1]);

		for (int k = 0; k < nr && !skip; k++)
			skip = !strcmp(found[i], removed[k]);
		if (!skip)
			snprintf(names[n++], NAME_LEN, "%.63s", found[i]);
	}
	return n;
}

/* The effective colors of the current mode: the theme, with the user's per-color
 * overrides on top, and the derived text and outline colors. */
void
theme_scheme(const Config *c, Scheme *s)
{
	int i, m = c->mode;

	for (i = 0; i < C_N; i++)
		snprintf(s->hex[i], TM_COLOR_LEN, "%s", c->color[i][0] ? c->color[i] : c->th.hex[m][i]);
	for (i = 0; i < 16; i++)
		snprintf(s->ansi[i], TM_COLOR_LEN, "%s", c->th.ansi[m][i]);
	/* outlines: fg eased toward bg, but never under 3:1 against bg */
	tm_mix(s->line, sizeof s->line, s->hex[C_FG], s->hex[C_BG], 3);
	if (tm_contrast(s->line, s->hex[C_BG]) < 3.0)
		snprintf(s->line, sizeof s->line, "%s", s->hex[C_FG]);
	/* text on the urgent and warning fills: each color gets its own best text */
	tm_text_on(s->on_urg, sizeof s->on_urg, s->hex[C_URG], s->hex[C_ACCFG]);
	tm_text_on(s->on_warn, sizeof s->on_warn, s->hex[C_WARN], s->hex[C_ACCFG]);
}
