/* state.c: the defaults from config.h and the state file, ~/.config/themer/state.
 * The file is key=value lines: theme, mode, fonts, icons, cursor, cursor_size,
 * hinting, antialias, rgba, set (the template set),
 * then color.<role> overrides, env.<NAME> variables, module.<name> on/off, and
 * opt.<module.option>. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
	c->web_port = 7331;
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

/* forgets an option the user set, so it follows its default again */
void
config_unset_option(Config *c, const char *key)
{
	for (int i = 0; i < c->nopt; i++)
		if (!strcmp(c->optv[i].key, key)) {
			memmove(&c->optv[i], &c->optv[i + 1], (size_t)(c->nopt - i - 1) * sizeof c->optv[0]);
			c->nopt--;
			return;
		}
}

static int
state_path(char *p, size_t n)
{
	return path_under_home(p, n, ".config/themer/state");
}

/* returns 0 if a state file was loaded, -1 if there is none */
/* The state file as this process last read or wrote it: its inode (each save is a
 * new file, renamed into place), size and time. state_changed compares. */
static struct { int known; dev_t dev; ino_t ino; off_t size; struct timespec mtime; } seen;

static void
stamp_st(int ok, const struct stat *st)
{
	seen.known = ok;
	if (ok) {
		seen.dev = st->st_dev;
		seen.ino = st->st_ino;
		seen.size = st->st_size;
		seen.mtime = st->st_mtim;
	}
}

static void
stamp(const char *p)
{
	struct stat st;
	int ok = stat(p, &st) == 0;

	stamp_st(ok, &st);
}

/* the file as opened: a save renamed into place after the open is still news */
static void
stamp_file(FILE *f)
{
	struct stat st;
	int ok = fstat(fileno(f), &st) == 0;

	stamp_st(ok, &st);
}

int
state_changed(void)
{
	char p[PATH_MAX];
	struct stat st;

	if (state_path(p, sizeof p) < 0)
		return 0;
	if (stat(p, &st) < 0)
		return 0;                 /* gone: nothing newer to read */
	return !seen.known || st.st_dev != seen.dev || st.st_ino != seen.ino || st.st_size != seen.size ||
	       st.st_mtim.tv_sec != seen.mtime.tv_sec || st.st_mtim.tv_nsec != seen.mtime.tv_nsec;
}

int
state_load(Config *c)
{
	char p[PATH_MAX], *line = NULL;
	size_t cap = 0;
	FILE *f;

	if (state_path(p, sizeof p) < 0 || !(f = fopen(p, "r")))
		return -1;
	stamp_file(f);
	while (getline(&line, &cap, f) > 0) {
		char *eq = strchr(line, '=');
		char *v;

		if (!eq)
			continue;
		*eq = '\0';
		v = eq + 1;
		v[strcspn(v, "\r\n")] = '\0';
		/* a value too long for its field is left out rather than cut short, which
		 * could make it a different, valid-looking value */
		if (strlen(line) >= 2 * NAME_LEN || strlen(v) >= 256 ||
		    (strncmp(line, "env.", 4) && strlen(v) >= NAME_LEN)) {
			fprintf(stderr, "themer: state: '%.40s' is too long; ignored\n", line);
			continue;
		}
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
		else if (!strcmp(line, "web_port") && atoi(v) >= 1024 && atoi(v) <= 65535) c->web_port = atoi(v);
		else if (!strcmp(line, "removed")) c->removed = atoi(v) != 0;
		else if (!strcmp(line, "hinting") && v[0]) snprintf(c->hinting, sizeof c->hinting, "%s", v);
		else if (!strcmp(line, "antialias")) c->antialias = atoi(v) != 0;
		else if (!strcmp(line, "rgba") && v[0]) snprintf(c->rgba, sizeof c->rgba, "%s", v);
		else if (!strcmp(line, "set") && v[0]) {
			/* the names Themer Chicago had in earlier builds, when it was the
			 * only built-in set */
			if (!strcmp(v, "Themer Chicago") || !strcmp(v, "default") ||
			    !strcmp(v, "Themer Chicago Mask") || !strcmp(v, "themer_chicago_mask"))
				v = "themer_chicago";
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
	           "tty_font=%s\nicons=%s\ncursor=%s\ncursor_size=%d\nweb_port=%d\n"
	           "hinting=%s\nantialias=%d\nrgba=%s\nset=%s\n",
	        c->th.name, c->mode == MODE_DARK ? "dark" : "light", c->ui_font, c->ui_size,
	        c->mono_font, c->mono_size, c->tty_font, c->icons, c->cursor, c->cursor_size, c->web_port,
	        c->hinting, c->antialias, c->rgba, c->set);
	if (c->removed)
		fprintf(f, "removed=1\n");
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

/* true if two configurations hold the same settings: compared as their state
 * files, so padding and the order options were set in do not count */
int
config_same(const Config *a, const Config *b)
{
	char *ta = NULL, *tb = NULL;
	size_t na = 0, nb = 0;
	FILE *fa = open_memstream(&ta, &na), *fb = open_memstream(&tb, &nb);
	int same = 0;

	if (fa && fb) {
		emit_state(fa, a, NULL);
		emit_state(fb, b, NULL);
		fclose(fa);
		fclose(fb);
		fa = fb = NULL;
		same = na == nb && !memcmp(ta, tb, na);
	}
	if (fa)
		fclose(fa);
	if (fb)
		fclose(fb);
	free(ta);
	free(tb);
	return same;
}

int
state_save(const Config *c)
{
	char p[PATH_MAX];

	if (state_path(p, sizeof p) < 0)
		return -1;
	if (write_gen(p, emit_state, c, NULL) < 0)
		return -1;
	stamp(p);
	return 0;
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

/* Into disk (the state another front end saved), the settings mine changed from
 * base (what mine started from): theirs where mine left a setting alone, mine
 * where mine changed it. */
void
config_merge(Config *disk, const Config *mine, const Config *base)
{
#define STR(f) if (strcmp(mine->f, base->f)) snprintf(disk->f, sizeof disk->f, "%s", mine->f)
#define NUM(f) if (mine->f != base->f) disk->f = mine->f
	if (strcmp(mine->th.name, base->th.name))
		disk->th = mine->th;
	NUM(mode);
	STR(ui_font); NUM(ui_size); STR(mono_font); NUM(mono_size); STR(tty_font);
	STR(icons); STR(cursor); NUM(cursor_size); NUM(web_port); NUM(removed);
	STR(hinting); NUM(antialias); STR(rgba); STR(set);
	for (int i = 0; i < C_N; i++)
		STR(color[i]);
#undef STR
#undef NUM
	/* the lists: a variable, module or option mine changed, added or removed */
	for (int i = 0; i < mine->nenv; i++) {
		const char *was = NULL;

		for (int k = 0; k < base->nenv; k++)
			if (!strcmp(base->env[k].name, mine->env[i].name))
				was = base->env[k].value;
		if (!was || strcmp(was, mine->env[i].value))
			env_set(disk, mine->env[i].name, mine->env[i].value);
	}
	for (int k = 0; k < base->nenv; k++) {
		int kept = 0;

		for (int i = 0; i < mine->nenv; i++)
			kept |= !strcmp(base->env[k].name, mine->env[i].name);
		if (!kept)
			env_set(disk, base->env[k].name, "");
	}
	for (int i = 0; i < mine->nmods; i++) {
		int was = -1;

		for (int k = 0; k < base->nmods; k++)
			if (!strcmp(base->mods[k].name, mine->mods[i].name))
				was = base->mods[k].on;
		if (was != mine->mods[i].on)
			config_set_module(disk, mine->mods[i].name, mine->mods[i].on);
	}
	for (int i = 0; i < mine->nopt; i++) {
		const char *was = config_option(base, mine->optv[i].key);

		if (!was || strcmp(was, mine->optv[i].value))
			config_set_option(disk, mine->optv[i].key, mine->optv[i].value);
	}
	for (int k = 0; k < base->nopt; k++)
		if (!config_option(mine, base->optv[k].key))
			config_unset_option(disk, base->optv[k].key);
}
