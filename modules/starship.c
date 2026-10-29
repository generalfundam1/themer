/* starship.c: the starship prompt's colors, as a palette.
 *
 * Starship's modules name their colors (green, bold purple, bright-black...), and a
 * name is looked up in the selected palette before the terminal's own. The module
 * adds a palette named themer (rendered from templates/<set>/starship/palette.toml)
 * that defines every one of those names as the theme's terminal color, and selects
 * it, in ~/.config/starship.toml (or $STARSHIP_CONFIG):
 *   - the [palettes.themer] table goes in a marked block at the end of the file;
 *   - palette = "themer" is a top-level key, which TOML only allows before the first
 *     table: the user's own palette line is changed in place when there is one, and
 *     put back on unload; otherwise the line goes in a marked block at the very top.
 * The prompt's layout and symbols stay the user's (or starship's defaults).
 *
 * Options (after the host's mode):
 *   template  the set it is drawn from (templates/<set>/starship/) */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "themer_module.h"

static const TmOption opts[] = {
	TM_OPT_TEMPLATE,
};

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("starship");
}

/* 1 when the file's top-level palette key is written without a space before the =,
 * which a line change could not find again to put back */
static int
palette_unspaced(const char *path)
{
	char line[512], *s;
	int found = 0;
	FILE *f = fopen(path, "r");

	if (!f)
		return 0;
	while (fgets(line, sizeof line, f)) {
		for (s = line; *s == ' ' || *s == '\t'; s++)
			;
		if (*s == '[')
			break;           /* the first table: no top-level keys after it */
		if (!strncmp(s, "palette", 7) && s[7] == '=')
			found = 1;
	}
	fclose(f);
	return found;
}

static int
load(const TmTheme *t)
{
	static char body[4096];
	const char *env = getenv("STARSHIP_CONFIG"), *line = "palette = \"themer\"";
	char p[PATH_MAX];
	int r;

	if (tm_render(body, sizeof body, "starship", tm_opt(t, "starship.template"), "palette.toml", t, NULL, 0) < 0)
		return -1;
	if (env && env[0])
		snprintf(p, sizeof p, "%s", env);
	else
		tm_path(p, sizeof p, t, ".config/starship.toml");
	if (palette_unspaced(p)) {
		fprintf(stderr, "themer: starship: %s has palette= with no space before the =; "
		        "write it as palette = \"...\" for themer to select its palette\n", p);
		return -1;
	}
	if (tm_blockf(p, "starship", "#", "", 0, "%s", body) < 0)
		return -1;
	if ((r = tm_setline(p, "palette", line)) < 0)
		return -1;
	return r == 1 ? tm_blockf(p, "starship-palette", "#", "", 1, "%s\n", line) : 0;
}

static const TmModule mod = {
	.name = "starship",
	.desc = "starship: the prompt's colors, as a palette of the theme's (starship.toml)",
	.templates = "starship",
	.default_on = 1,
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
