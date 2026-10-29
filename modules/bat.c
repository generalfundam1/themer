/* bat.c: bat's syntax colors, from the terminal's.
 *
 * Adds --theme to a marked block at the end of bat's config file
 * (~/.config/bat/config, or $BAT_CONFIG_PATH), where it comes after the user's own
 * options and so is the one bat uses. The theme is one of bat's own that draws in
 * the terminal's palette instead of fixed colors: "ansi" (the eight colors) or
 * "base16" (the sixteen). The terminal modules set that palette from the theme, so
 * bat follows every theme and both modes with nothing generated; a theme of fixed
 * colors would need bat's cache rebuilt on every apply, and would be wrong in a
 * terminal themer does not reach.
 *
 * Options:
 *   theme   ansi or base16 (both in the terminal's colors) */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include "themer_module.h"

static const TmOption opts[] = {
	{ "theme", "bat's theme in the terminal's colors: ansi uses the eight, base16 all sixteen", "ansi|base16", "ansi", 0, 0 },
};

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("bat") || tm_have("batcat");   /* batcat on Debian and Ubuntu */
}

static int
load(const TmTheme *t)
{
	const char *theme = tm_opt(t, "bat.theme"), *env = getenv("BAT_CONFIG_PATH");
	char p[PATH_MAX];

	if (env && env[0])
		snprintf(p, sizeof p, "%s", env);
	else
		tm_path(p, sizeof p, t, ".config/bat/config");
	return tm_blockf(p, "bat", "#", "", 0, "--theme=\"%s\"\n", theme ? theme : "ansi");
}

static const TmModule mod = {
	.name = "bat",
	.desc = "bat: syntax colors from the terminal's palette (--theme in bat's config)",
	.default_on = 1,
	.colorless = 1,          /* the colors are the terminal's: no mode to pin */
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
