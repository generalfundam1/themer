/* helix.c: a Helix theme named "themer", and the line that selects it.
 *
 * Writes:
 *   ~/.config/helix/themes/themer.toml   templates/<set>/helix/themer.toml: the
 *                                        interface in the theme's roles, the syntax
 *                                        in its terminal colors
 *   ~/.config/helix/config.toml          theme = "themer": your theme line changed
 *                                        in place (put back on unload), or, when you
 *                                        have none, a marked block at the top, where
 *                                        a top-level key must go (before any table)
 * Helix reads both at start; a running one takes them with :config-reload.
 * A theme line written theme="..." (no space before the =) is one the line change
 * cannot find again to put back, and a second theme key next to it would stop Helix
 * reading the file ("duplicate key"), so the module then leaves config.toml alone
 * and says why.
 *
 * Options (after the host's mode):
 *   template  the set it is drawn from (templates/<set>/helix/) */
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "themer_module.h"

static const TmOption opts[] = {
	TM_OPT_TEMPLATE,
};

/* hx (helix on Arch), or the Flatpak */
static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("hx") || tm_have("helix") || tm_have_flatpak("com.helix_editor.Helix");
}

/* 1 when the file's top-level theme key is written with no space before the = */
static int
theme_unspaced(const char *path)
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
		if (!strncmp(s, "theme", 5) && s[5] == '=')
			found = 1;
	}
	fclose(f);
	return found;
}

static int
load(const TmTheme *t)
{
	static char body[65536];
	char p[PATH_MAX];
	int r;

	if (tm_render(body, sizeof body, "helix", tm_opt(t, "helix.template"), "themer.toml", t, NULL, 0) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/helix/themes/themer.toml");
	if (tm_writef(p, "%s", body) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/helix/config.toml");
	if (theme_unspaced(p)) {
		fprintf(stderr, "themer: helix: %s has theme= with no space before the =; "
		        "write it as theme = \"...\" for themer to select its theme\n", p);
		return -1;
	}
	r = tm_setline(p, "theme", "theme = \"themer\"");
	if (r == 1)   /* no theme line of yours (or no config): a block at the top */
		r = tm_blockf(p, "helix", "#", "", 1, "theme = \"themer\"");
	return r < 0 ? -1 : 0;
}

static const TmModule mod = {
	.name = "helix",
	.desc = "Helix: the themer theme, selected in config.toml",
	.templates = "helix",
	.default_on = 1,
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
