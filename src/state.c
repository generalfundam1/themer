/* state.c: the defaults from config.h and the state file, ~/.config/themer/state.
 * The file is key=value lines: theme, mode, fonts, icons, cursor, cursor_size,
 * hinting, antialias, rgba, set (the template set),
 * then color.<role> overrides, env.<NAME> variables, module.<name> on/off, and
 * opt.<module.option>. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "themer.h"

int
config_defaults(Config *c)
{
	memset(c, 0, sizeof *c);
	c->mode = MODE_DARK;
	snprintf(c->ui_font, sizeof c->ui_font, "%s", UI_FONT);
	c->ui_size = UI_SIZE;
	snprintf(c->mono_font, sizeof c->mono_font, "%s", MONO_FONT);
	c->mono_size = MONO_SIZE;
	snprintf(c->tty_font, sizeof c->tty_font, "%s", TTY_FONT);
	snprintf(c->icons, sizeof c->icons, "%s", ICON_THEME);
	snprintf(c->cursor, sizeof c->cursor, "%s", CURSOR_THEME);
	c->cursor_size = CURSOR_SIZE;
	snprintf(c->hinting, sizeof c->hinting, "%s", HINTING);
	c->antialias = ANTIALIAS;
	snprintf(c->rgba, sizeof c->rgba, "%s", RGBA);
	snprintf(c->set, sizeof c->set, "%s", TM_DEFAULT_SET);
	return theme_load(DEF_THEME, &c->th);
}

/* sets an environment variable; an empty value deletes it */
void
env_set(Config *c, const char *name, const char *value)
{
	int i;

	for (i = 0; i < c->nenv; i++)
		if (!strcmp(c->env[i].name, name))
			break;
	if (!value[0]) {
		if (i < c->nenv) {
			memmove(&c->env[i], &c->env[i + 1], (c->nenv - i - 1) * sizeof c->env[0]);
			c->nenv--;
		}
		return;
	}
	if (i == c->nenv) {
		if (c->nenv == MAX_ENV)
			return;
		c->nenv++;
		snprintf(c->env[i].name, sizeof c->env[i].name, "%s", name);
	}
	snprintf(c->env[i].value, sizeof c->env[i].value, "%s", value);
}

static int
color_index(const char *name)
{
	for (int i = 0; i < C_N; i++)
		if (!strcmp(color_names[i], name))
			return i;
	return -1;
}

/* the stored on/off state of a module, or dflt when the user never changed it */
int
config_module_on(const Config *c, const char *name, int dflt)
{
	for (int i = 0; i < c->nmods; i++)
		if (!strcmp(c->mods[i].name, name))
			return c->mods[i].on;
	return dflt;
}

void
config_set_module(Config *c, const char *name, int on)
{
	for (int i = 0; i < c->nmods; i++)
		if (!strcmp(c->mods[i].name, name)) {
			c->mods[i].on = on;
			return;
		}
	if (c->nmods < MAX_MODULES) {
		snprintf(c->mods[c->nmods].name, NAME_LEN, "%s", name);
		c->mods[c->nmods].on = on;
		c->nmods++;
	}
}

/* the stored value of a module option ("module.option"), or NULL for the default */
const char *
config_option(const Config *c, const char *key)
{
	for (int i = 0; i < c->nopt; i++)
		if (!strcmp(c->optv[i].key, key))
			return c->optv[i].value;
	return NULL;
}

void
config_set_option(Config *c, const char *key, const char *value)
{
	int i;

	for (i = 0; i < c->nopt; i++)
		if (!strcmp(c->optv[i].key, key))
			break;
	if (i == c->nopt) {
		if (c->nopt == MAX_MODOPTS)
			return;
		c->nopt++;
		snprintf(c->optv[i].key, sizeof c->optv[i].key, "%s", key);
	}
	snprintf(c->optv[i].value, sizeof c->optv[i].value, "%s", value);
}

static int
state_path(char *p, size_t n)
{
	return path_under_home(p, n, ".config/themer/state");
}

/* returns 0 if a state file was loaded, -1 if there is none */
int
state_load(Config *c)
{
	char p[PATH_MAX], *line = NULL;
	size_t cap = 0;
	FILE *f;

	if (state_path(p, sizeof p) < 0 || !(f = fopen(p, "r")))
		return -1;
	while (getline(&line, &cap, f) > 0) {
		char *eq = strchr(line, '=');
		char *v;

		if (!eq)
			continue;
		*eq = '\0';
		v = eq + 1;
		v[strcspn(v, "\r\n")] = '\0';
		if (!strcmp(line, "theme")) {
			Theme t;

			if (theme_load(v, &t) == 0)
				c->th = t;
		}
		/* "1" is how older state files said light */
		else if (!strcmp(line, "mode"))       c->mode = !strcmp(v, "light") || !strcmp(v, "1") ? MODE_LIGHT : MODE_DARK;
		else if (!strcmp(line, "ui_font"))    snprintf(c->ui_font, sizeof c->ui_font, "%s", v);
		else if (!strcmp(line, "ui_size"))    c->ui_size = atoi(v);
		else if (!strcmp(line, "mono_font"))  snprintf(c->mono_font, sizeof c->mono_font, "%s", v);
		else if (!strcmp(line, "mono_size"))  c->mono_size = atoi(v);
		else if (!strcmp(line, "tty_font"))   snprintf(c->tty_font, sizeof c->tty_font, "%s", v);
		else if (!strcmp(line, "icons"))      snprintf(c->icons, sizeof c->icons, "%s", v);
		else if (!strcmp(line, "cursor") && v[0]) snprintf(c->cursor, sizeof c->cursor, "%s", v);
		else if (!strcmp(line, "cursor_size") && atoi(v) > 0) c->cursor_size = atoi(v);
		else if (!strcmp(line, "hinting") && v[0]) snprintf(c->hinting, sizeof c->hinting, "%s", v);
		else if (!strcmp(line, "antialias")) c->antialias = atoi(v) != 0;
		else if (!strcmp(line, "rgba") && v[0]) snprintf(c->rgba, sizeof c->rgba, "%s", v);
		else if (!strcmp(line, "set") && v[0]) {
			/* the built-in sets' names in earlier builds */
			if (!strcmp(v, "Themer Chicago") || !strcmp(v, "default") ||
			    !strcmp(v, "Themer Chicago Mask") || !strcmp(v, "themer_chicago_mask"))
				v = TM_DEFAULT_SET;
			snprintf(c->set, sizeof c->set, "%s", v);
		}
		else if (!strncmp(line, "color.", 6)) {
			int i = color_index(line + 6);

			if (i >= 0)
				snprintf(c->color[i], TM_COLOR_LEN, "%s", v);
		} else if (!strncmp(line, "env.", 4))
			env_set(c, line + 4, v);
		else if (!strncmp(line, "module.", 7))
			config_set_module(c, line + 7, atoi(v));
		else if (!strncmp(line, "opt.", 4))
			config_set_option(c, line + 4, v);
	}
	free(line);
	fclose(f);
	return 0;
}

static void
emit_state(FILE *f, const Config *c, const Scheme *s)
{
	(void)s;
	fprintf(f, "theme=%s\nmode=%s\nui_font=%s\nui_size=%d\nmono_font=%s\nmono_size=%d\n"
	           "tty_font=%s\nicons=%s\ncursor=%s\ncursor_size=%d\n"
	           "hinting=%s\nantialias=%d\nrgba=%s\nset=%s\n",
	        c->th.name, c->mode == MODE_DARK ? "dark" : "light", c->ui_font, c->ui_size,
	        c->mono_font, c->mono_size, c->tty_font, c->icons, c->cursor, c->cursor_size,
	        c->hinting, c->antialias, c->rgba, c->set);
	for (int i = 0; i < C_N; i++)
		if (c->color[i][0])
			fprintf(f, "color.%s=%s\n", color_names[i], c->color[i]);
	for (int i = 0; i < c->nenv; i++)
		fprintf(f, "env.%s=%s\n", c->env[i].name, c->env[i].value);
	for (int i = 0; i < c->nmods; i++)
		fprintf(f, "module.%s=%d\n", c->mods[i].name, c->mods[i].on);
	for (int i = 0; i < c->nopt; i++)
		fprintf(f, "opt.%s=%s\n", c->optv[i].key, c->optv[i].value);
}

int
state_save(const Config *c)
{
	char p[PATH_MAX];

	if (state_path(p, sizeof p) < 0)
		return -1;
	return write_gen(p, emit_state, c, NULL);
}

int
themer_startup(Config *c)
{
	static const char *def_env[][2] = DEF_ENV;

	if (config_defaults(c) < 0) {
		fprintf(stderr, "themer: default theme '%s' not found\n", DEF_THEME);
		return -1;
	}
	if (state_load(c) < 0)
		for (size_t i = 0; i < TM_COUNT(def_env); i++)
			env_set(c, def_env[i][0], def_env[i][1]);
	if (template_sync() < 0)
		fprintf(stderr, "themer: could not update ~/.config/themer/templates\n");
	modules_load();
	return 0;
}
