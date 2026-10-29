/* alacritty.c: Alacritty's colors and font.
 *
 * Writes ~/.config/alacritty/themer.toml, rendered from templates/<set>/alacritty/
 * themer.toml, and adds it to the imports of the user's alacritty.toml (general.import,
 * Alacritty 0.14 and later). TOML allows a table only once and wants keys of the root
 * before any table, so the import goes in where the file already has one:
 *   an import line            the path joins its list: last in a list on one line
 *                             (so it wins over the other imports), first in one
 *                             spread over lines (the line is changed in place and
 *                             put back on unload)
 *   a [general] table         an import line in a marked block right under it
 *   neither, or no file       general.import, a key of the root, in a marked block
 *                             at the top
 * Alacritty reads imports before the file itself, so colors set in alacritty.toml
 * still win over the theme's: take them out to follow the theme. Alacritty watches
 * its config and the files it imports (live_config_reload, on by default).
 *
 * Options (after the host's mode):
 *   font      terminal (the default) or ui
 *   template  the set it is drawn from */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "themer_module.h"

#define IMPORT "~/.config/alacritty/themer.toml"

static const TmOption opts[] = {
	{ "font", "font for the terminal", "terminal|ui", "terminal", 0, 0 },
	TM_OPT_TEMPLATE,
};

static int
template_vars(const TmTheme *t, TmVar *v, int max)
{
	static char fam[128], size[16];
	int sz;

	if (max < 2)
		return 0;
	tm_font_for(t, "alacritty.font", fam, sizeof fam, &sz);
	snprintf(size, sizeof size, "%d", sz);
	v[0] = (TmVar){ "font_family", fam };
	v[1] = (TmVar){ "font_size", size };
	return 2;
}

TM_VARS(template_vars)

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("alacritty");
}

static const char *
skip_ws(const char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	return s;
}

/* The last line of the file that starts with the word key (as tm_setline matches
 * it), into out without its newline; 0 when found. */
static int
last_line(const char *path, const char *key, char *out, size_t n)
{
	char *line = NULL;
	size_t cap = 0, kl = strlen(key);
	int found = 0;
	FILE *f = fopen(path, "r");

	if (!f)
		return -1;
	while (getline(&line, &cap, f) > 0) {
		const char *s = skip_ws(line);

		if (!strncmp(s, key, kl) && (s[kl] == ' ' || s[kl] == '\t' || s[kl] == '=' ||
		    s[kl] == '\n' || s[kl] == '\r' || !s[kl])) {
			snprintf(out, n, "%.*s", (int)strcspn(line, "\r\n"), line);
			found = 1;
		}
	}
	free(line);
	fclose(f);
	return found ? 0 : -1;
}

/* the import line with ours added: before the closing ] of a list on one line,
 * after the [ of one that goes on over the next lines; 0 when it could be done */
static int
add_import(const char *line, char *out, size_t n)
{
	const char *open = strchr(line, '['), *close = strrchr(line, ']');

	if (strstr(line, IMPORT)) {
		snprintf(out, n, "%s", line);    /* already in it */
		return 0;
	}
	if (!open)
		return -1;
	if (close && close > open) {
		const char *c = close;

		while (c > open + 1 && (c[-1] == ' ' || c[-1] == '\t'))
			c--;
		snprintf(out, n, "%.*s%s\"" IMPORT "\"%s", (int)(c - line), line,
		         c > open + 1 && c[-1] != ',' ? ", " : "", close);
	} else {
		snprintf(out, n, "%.*s\"" IMPORT "\",%s%s", (int)(open + 1 - line), line,
		         *skip_ws(open + 1) ? " " : "", open + 1);
	}
	return 0;
}

static int
load(const TmTheme *t)
{
	char body[8192], p[PATH_MAX], line[4096], next[4200];
	TmVar v[2];
	int nv = template_vars(t, v, TM_COUNT(v));

	if (tm_render(body, sizeof body, "alacritty", tm_opt(t, "alacritty.template"), "themer.toml", t, v, nv) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/alacritty/themer.toml");
	if (tm_writef(p, "%s", body) < 0)
		return -1;

	tm_path(p, sizeof p, t, ".config/alacritty/alacritty.toml");
	if (last_line(p, "import", line, sizeof line) == 0) {
		if (add_import(line, next, sizeof next) < 0 || (strcmp(line, next) && tm_setline(p, "import", next))) {
			fprintf(stderr, "themer: alacritty: could not add %s to the import line of %s; "
			        "write it as import = [ ... ] on one line\n", IMPORT, p);
			return -1;
		}
		return 0;
	}
	if (last_line(p, "[general]", line, sizeof line) == 0)
		return tm_blockf_after(p, "alacritty", "#", "", "[general]", "import = [\"" IMPORT "\"]");
	return tm_blockf(p, "alacritty", "#", "", 1, "general.import = [\"" IMPORT "\"]");
}

static const TmModule mod = {
	.name = "alacritty",
	.desc = "Alacritty: the terminal's colors and font (themer.toml, imported from alacritty.toml)",
	.templates = "alacritty",
	.default_on = 1,
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
