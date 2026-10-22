/* front.c: what every front end does the same way. The command line, the TUI,
 * themer-gtk, and themer-qt all change the configuration through these, so a
 * setting is validated and applied identically whichever one was used: the TUI is
 * the GUI without the previews, and the CLI is both without the screen. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "themer.h"

int
front_set_theme(Config *c, const char *name)
{
	Theme t;

	if (theme_load(name, &t) < 0)
		return -1;
	c->th = t;
	return 0;
}

int
front_set_set(Config *c, const char *name)
{
	char sets[MAX_NAMES][NAME_LEN];
	int n = template_all_sets(sets, MAX_NAMES);

	for (int i = 0; i < n; i++)
		if (!strcmp(sets[i], name)) {
			snprintf(c->set, sizeof c->set, "%s", name);
			return 0;
		}
	return -1;
}

int
front_set_font(Config *c, int mono, const char *family, int size)
{
	char *fam = mono ? c->mono_font : c->ui_font;
	int *sz = mono ? &c->mono_size : &c->ui_size;

	if (!family || !family[0] || size < 0 || size > 200)
		return -1;
	snprintf(fam, sizeof c->ui_font, "%s", family);
	if (size > 0)
		*sz = size;
	return 0;
}

int
front_set_icons(Config *c, const char *name)
{
	if (!name || !name[0])
		return -1;
	snprintf(c->icons, sizeof c->icons, "%s", name);
	return 0;
}

int
front_set_cursor(Config *c, const char *name, int size)
{
	if (size < 0 || size > 512 || (name && !name[0]))
		return -1;
	if (name)
		snprintf(c->cursor, sizeof c->cursor, "%s", name);
	if (size > 0)
		c->cursor_size = size;
	return 0;
}

int
front_set_hinting(Config *c, const char *value)
{
	for (int i = 0; i < 4; i++)
		if (!strcmp(hinting_values[i], value)) {
			snprintf(c->hinting, sizeof c->hinting, "%s", value);
			return 0;
		}
	return -1;
}

int
front_set_rgba(Config *c, const char *value)
{
	for (int i = 0; i < 5; i++)
		if (!strcmp(rgba_values[i], value)) {
			snprintf(c->rgba, sizeof c->rgba, "%s", value);
			return 0;
		}
	return -1;
}

void
front_set_antialias(Config *c, int on)
{
	c->antialias = on != 0;
}

/* Copies set from under the name to in ~/.config/themer/templates, every module
 * directory it covers; the report gets one line per directory. Returns the number
 * of files copied, -1 on error, -2 if from has nothing or the names are the same. */
int
front_set_copy(const char *from, const char *to, FILE *report)
{
	char dirs[MAX_NAMES][NAME_LEN];
	int nd = template_dirs(dirs, MAX_NAMES), total = 0;

	if (!strcmp(from, to) || !valid_name(to) || !strcmp(to, TM_DEFAULT_SET))
		return -2;
	for (int k = 0; k < nd; k++) {
		int n;

		if (!template_has(dirs[k], from))
			continue;
		n = template_copy(dirs[k], from, to);
		if (n < 0)
			return -1;
		if (report)
			fprintf(report, "%s/%s: %d file(s)\n", to, dirs[k], n);
		total += n;
	}
	return total ? total : -2;
}

/* Sets a module option and saves the state (a loaded module reloads at once, as
 * themer_option_set does). Returns 0, or -1 with the reason in msg. */
int
front_option_set(Config *c, int module, int option, const char *value, char *msg, size_t n)
{
	int r = themer_option_set(c, module, option, value);

	if (r == -1) {
		snprintf(msg, n, "'%s' is not a valid value for %s", value, themer_option(module, option)->name);
		return -1;
	}
	if (r == -2) {
		snprintf(msg, n, "%s did not load with '%s'; the previous value was kept",
		         themer_module_name(module), value);
		return -1;
	}
	if (state_save(c) < 0) {
		snprintf(msg, n, "set, but the state file could not be saved");
		return -1;
	}
	snprintf(msg, n, "%s.%s = %s", themer_module_name(module), themer_option(module, option)->name, value);
	return 0;
}

int
front_apply(Config *c)
{
	int fails = themer_apply(c);

	if (state_save(c) < 0)
		fails++;
	return fails;
}

static int
cmp_module(const void *a, const void *b)
{
	return strcmp(themer_module_name(*(const int *)a), themer_module_name(*(const int *)b));
}

int
front_module_order(int *order, int max)
{
	int n = themer_module_count() < max ? themer_module_count() : max;

	for (int i = 0; i < n; i++)
		order[i] = i;
	qsort(order, n, sizeof order[0], cmp_module);
	return n;
}

int
front_render(const Config *c, const char *module, const char *dir, const char *file,
             char *out, size_t n)
{
	Scheme s;
	TmTheme t;
	TmVar v[128];
	int i = module ? themer_module_find(module) : -1, nv = 0;

	theme_scheme(c, &s);
	themer_view(c, &s, &t);
	if (i >= 0)
		nv = themer_module_vars(i, &t, v, (int)TM_COUNT(v));
	if (nv < 0)
		return -1;
	return tm_render(out, n, dir, c->set, file, &t, v, nv);
}
