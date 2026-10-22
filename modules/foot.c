/* foot.c: terminal font and colors in the user's foot.ini.
 * Sets keys in place. The colors go in the [colors-dark] or [colors-light]
 * section that matches the mode. tm_ini uses the last such section, which is
 * the live one in files that repeat a section.
 * The font is set in [main]. A top-level font= line above the first section
 * (as in many hand-written foot.ini files) is updated too, so it can't
 * override the new font. */
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "themer_module.h"

/* true if the file has a font= line before its first [section] */
static int
top_level_font(const char *path)
{
	char line[512];
	int found = 0;
	FILE *f = fopen(path, "r");

	if (!f)
		return 0;
	while (fgets(line, sizeof line, f) && line[0] != '[') {
		if (!strncmp(line, "font", 4) && line[4] == '=')
			found = 1;
	}
	fclose(f);
	return found;
}

/* foot, or a config for it */
static int
present(const TmTheme *t)
{
	char p[PATH_MAX];

	tm_path(p, sizeof p, t, ".config/foot/foot.ini");
	return !access(p, F_OK) || tm_have("foot");
}

static int
load(const TmTheme *t)
{
	char p[PATH_MAX], sect[32], font[128], key[16];

	tm_path(p, sizeof p, t, ".config/foot/foot.ini");

	snprintf(sect, sizeof sect, "colors-%s", t->dark ? "dark" : "light");
	snprintf(font, sizeof font, "%s:size=%d", t->mono_font, t->mono_size);

	tm_ini(p, "main", "font", font);
	if (top_level_font(p))
		tm_ini(p, "", "font", font);
	tm_ini(p, "main", "initial-color-theme", t->dark ? "dark" : "light");
	tm_ini(p, sect, "background", t->bg);
	tm_ini(p, sect, "foreground", t->fg);
	tm_ini(p, sect, "selection-background", t->acc);
	tm_ini(p, sect, "selection-foreground", t->accfg);
	for (int i = 0; i < 8; i++) {
		snprintf(key, sizeof key, "regular%d", i);
		tm_ini(p, sect, key, t->ansi[i]);
		snprintf(key, sizeof key, "bright%d", i);
		tm_ini(p, sect, key, t->ansi[i + 8]);
	}
	return 0;
}

static const TmModule mod = {
	.name = "foot",
	.desc = "terminal font and colors, set in foot.ini",
	.default_on = 1,
	.present = present,
	.load = load,
};
TM_EXPORT(mod)
