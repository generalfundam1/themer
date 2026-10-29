/* wezterm.c: WezTerm's colors, font and tab bar.
 *
 * Writes a color scheme, ~/.config/themer/wezterm/themer.toml, rendered from
 * templates/<set>/wezterm/scheme.toml, and selects it from the user's wezterm.lua
 * (~/.wezterm.lua when it exists, else ~/.config/wezterm/wezterm.lua): a marked block,
 * rendered from select.lua, right after the line that makes the config table
 * ("local config = wezterm.config_builder()" or "local config = {}"). The block adds
 * the scheme's directory, picks the scheme, sets the font and the window frame;
 * lines further down the user's file still win. Without a wezterm.lua the block is
 * a whole config of its own, which unload takes away again. A wezterm.lua of another
 * shape (one that returns a table it builds in place) is left alone: the scheme is
 * written, and a message says the line to add. The block names the theme and its
 * main colors, so the file changes with the theme and WezTerm, which watches its
 * config, reloads.
 *
 * Options (after the host's mode):
 *   font      terminal (the default) or ui
 *   template  the set it is drawn from */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "themer_module.h"

static const TmOption opts[] = {
	{ "font", "font for the terminal", "terminal|ui", "terminal", 0, 0 },
	TM_OPT_TEMPLATE,
};

/* font_family, font_size, and scheme_dir, where the scheme is written */
static int
template_vars(const TmTheme *t, TmVar *v, int max)
{
	static char fam[128], size[16], dir[PATH_MAX];
	int sz;

	if (max < 3)
		return 0;
	tm_font_for(t, "wezterm.font", fam, sizeof fam, &sz);
	snprintf(size, sizeof size, "%d", sz);
	tm_path(dir, sizeof dir, t, ".config/themer/wezterm");
	v[0] = (TmVar){ "font_family", fam };
	v[1] = (TmVar){ "font_size", size };
	v[2] = (TmVar){ "scheme_dir", dir };
	return 3;
}

TM_VARS(template_vars)

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("wezterm") || tm_have_flatpak("org.wezfurlong.wezterm");
}

/* 1 when a line of the file starts with "local config" (the config table) */
static int
has_config_table(const char *path)
{
	char line[1024];
	int found = 0;
	FILE *f = fopen(path, "r");

	if (!f)
		return 0;
	while (!found && fgets(line, sizeof line, f)) {
		const char *s = line;

		while (*s == ' ' || *s == '\t')
			s++;
		found = !strncmp(s, "local config", 12) && (s[12] == ' ' || s[12] == '\t');
	}
	fclose(f);
	return found;
}

static int
load(const TmTheme *t)
{
	const char *set = tm_opt(t, "wezterm.template");
	char body[8192], p[PATH_MAX];
	TmVar v[3];
	int nv = template_vars(t, v, TM_COUNT(v));

	if (tm_render(body, sizeof body, "wezterm", set, "scheme.toml", t, v, nv) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/themer/wezterm/themer.toml");
	if (tm_writef(p, "%s", body) < 0)
		return -1;
	if (tm_render(body, sizeof body, "wezterm", set, "select.lua", t, v, nv) < 0)
		return -1;

	tm_path(p, sizeof p, t, ".wezterm.lua");
	if (access(p, F_OK))
		tm_path(p, sizeof p, t, ".config/wezterm/wezterm.lua");
	if (access(p, F_OK))
		return tm_blockf(p, "wezterm", "--", "", 0,
		                 "local wezterm = require 'wezterm'\n"
		                 "local config = wezterm.config_builder()\n%s\nreturn config", body);
	if (has_config_table(p))
		return tm_blockf_after(p, "wezterm", "--", "", "local config", "%s", body);
	fprintf(stderr, "themer: wezterm: %s has no \"local config = ...\" line to follow; "
	        "the theme is the color scheme \"themer\" in %s: add color_scheme = \"themer\" and "
	        "color_scheme_dirs = { \"%s\" } to your config\n", p, v[2].value, v[2].value);
	return 0;
}

static const TmModule mod = {
	.name = "wezterm",
	.desc = "WezTerm: the terminal's colors, font and tab bar (a color scheme, selected from wezterm.lua)",
	.templates = "wezterm",
	.default_on = 1,
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
