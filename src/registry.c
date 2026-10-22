/* registry.c: the modules found on disk, their options, the TmTheme view they are
 * handed, and apply. Modules are shared objects loaded with dlopen from
 * $THEMER_MODULES, ~/.config/themer/modules, and SYS_MODULES_DIR, in that order;
 * the first module with a given name wins. */
#include <dirent.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "themer.h"

typedef struct {
	const TmModule *m;
	void *h;
	int (*vars)(const TmTheme *, TmVar *, int);  /* optional: template variables */
} Reg;

static Reg reg[MAX_MODULES];
static int nreg;

/* what the modules are handed: option values and loaded names, rebuilt per view */
static TmOpt view_opts[MAX_MODOPTS];
static char view_keys[MAX_MODOPTS][2 * NAME_LEN];
static char view_vals[MAX_MODOPTS][NAME_LEN];
static const char *view_loaded[MAX_MODULES];

/* ---- discovery ---- */

static void
load_dir(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;

	if (!d)
		return;
	while ((e = readdir(d)) && nreg < MAX_MODULES) {
		size_t len = strlen(e->d_name);
		char p[PATH_MAX];
		const TmModule *(*init)(void);
		int (*abi)(void);
		int (*vars)(const TmTheme *, TmVar *, int);
		const TmModule *m;
		void *h;

		if (len < 4 || strcmp(e->d_name + len - 3, ".so"))
			continue;
		snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
		if (!(h = dlopen(p, RTLD_NOW | RTLD_LOCAL))) {
			fprintf(stderr, "themer: %s\n", dlerror());
			continue;
		}
		/* a module built for another TmModule layout would read garbage: refuse it */
		*(void **)&abi = dlsym(h, "themer_module_abi");
		if (!abi || abi() != TM_ABI) {
			fprintf(stderr, "themer: skipping %s: built for another themer version, rebuild it\n",
			        e->d_name);
			dlclose(h);
			continue;
		}
		*(void **)&init = dlsym(h, "themer_module");
		if (!init || !(m = init()) || !m->name || !m->load ||
		    themer_module_find(m->name) >= 0) {
			dlclose(h);
			continue;
		}
		*(void **)&vars = dlsym(h, "themer_module_vars");
		reg[nreg].m = m;
		reg[nreg].h = h;
		reg[nreg].vars = vars;
		nreg++;
	}
	closedir(d);
}

void
modules_load(void)
{
	char dir[PATH_MAX];
	const char *env = getenv("THEMER_MODULES");

	if (env)
		load_dir(env);
	if (path_under_home(dir, sizeof dir, ".config/themer/modules") == 0)
		load_dir(dir);
	load_dir(SYS_MODULES_DIR);
}

/* ---- lookup ---- */

int themer_module_count(void)              { return nreg; }
const char *themer_module_name(int i)      { return reg[i].m->name; }
const char *themer_module_desc(int i)      { return reg[i].m->desc; }
const char *themer_module_templates(int i) { return reg[i].m->templates; }
int themer_option_count(int i)             { return reg[i].m->noptions; }
const TmOption *themer_option(int i, int j) { return &reg[i].m->options[j]; }

int
themer_module_find(const char *name)
{
	for (int i = 0; i < nreg; i++)
		if (!strcmp(reg[i].m->name, name))
			return i;
	return -1;
}

int
themer_option_find(int i, const char *name)
{
	for (int j = 0; j < reg[i].m->noptions; j++)
		if (!strcmp(reg[i].m->options[j].name, name))
			return j;
	return -1;
}

int
themer_module_on(const Config *c, int i)
{
	return config_module_on(c, reg[i].m->name, reg[i].m->default_on);
}

/* whether the program a module themes is here; modules without a check are present */
int
themer_module_present(const Config *c, int i)
{
	Scheme s;
	TmTheme t;

	if (!reg[i].m->present)
		return 1;
	theme_scheme(c, &s);
	themer_view(c, &s, &t);
	return reg[i].m->present(&t) != 0;
}

/* a module's own template variables; 0 when it has none */
int
themer_module_vars(int i, const TmTheme *t, TmVar *v, int max)
{
	return reg[i].vars ? reg[i].vars(t, v, max) : 0;
}

/* ---- options ---- */

static void
option_key(char *key, size_t n, int i, int j)
{
	snprintf(key, n, "%s.%s", reg[i].m->name, reg[i].m->options[j].name);
}

/* The stored value, or the module's default. A module's "template" option that the
 * user never set follows the global template set (themer set NAME), so one switch
 * changes every module; a value set on the module itself stays as it is. */
const char *
themer_option_value(const Config *c, int i, int j)
{
	const TmOption *o = &reg[i].m->options[j];
	char key[2 * NAME_LEN];
	const char *v;

	option_key(key, sizeof key, i, j);
	v = config_option(c, key);
	if (v)
		return v;
	if (!strcmp(o->name, "template") && !strcmp(o->def, TM_DEFAULT_SET))
		return c->set;
	return o->def;
}

/* true if value is one of the option's choices, in its range, or any text */
static int
option_valid(const TmOption *o, const char *value)
{
	const char *p;
	size_t len = strlen(value);

	if (!o->choices && o->max > o->min) {
		char *end;
		long v;

		if (!len)
			return 0;
		v = strtol(value, &end, 10);
		return !*end && v >= o->min && v <= o->max;
	}
	if (!o->choices)
		return len > 0;
	for (p = o->choices; *p;) {
		const char *bar = strchr(p, '|');
		size_t n = bar ? (size_t)(bar - p) : strlen(p);

		if (n == len && !strncmp(p, value, n))
			return 1;
		if (!bar)
			break;
		p = bar + 1;
	}
	return 0;
}

/* ---- the state of the world the modules see ---- */

void
themer_view(const Config *c, const Scheme *s, TmTheme *t)
{
	int n = 0, nl = 0;

	memset(t, 0, sizeof *t);
	t->theme = c->th.name;
	t->dark = c->mode == MODE_DARK;
	t->bg = s->hex[C_BG];
	t->fg = s->hex[C_FG];
	t->surf = s->hex[C_SURF];
	t->acc = s->hex[C_ACC];
	t->accfg = s->hex[C_ACCFG];
	t->urg = s->hex[C_URG];
	t->warn = s->hex[C_WARN];
	t->line = s->line;
	t->on_urg = s->on_urg;
	t->on_warn = s->on_warn;
	for (int i = 0; i < 16; i++)
		t->ansi[i] = s->ansi[i];
	t->ui_font = c->ui_font;
	t->ui_size = c->ui_size;
	t->mono_font = c->mono_font;
	t->mono_size = c->mono_size;
	t->tty_font = c->tty_font;
	t->icons = c->icons;
	t->cursor = c->cursor;
	t->cursor_size = c->cursor_size;
	t->hinting = c->hinting;
	t->antialias = c->antialias;
	t->rgba = c->rgba;
	t->home = getenv("HOME");
	if (!t->home)
		t->home = "";

	for (int i = 0; i < nreg; i++) {
		const TmModule *m = reg[i].m;

		for (int j = 0; j < m->noptions && n < MAX_MODOPTS; j++) {
			option_key(view_keys[n], sizeof view_keys[n], i, j);
			snprintf(view_vals[n], sizeof view_vals[n], "%s", themer_option_value(c, i, j));
			view_opts[n].key = view_keys[n];
			view_opts[n].value = view_vals[n];
			n++;
		}
		if (themer_module_on(c, i))
			view_loaded[nl++] = m->name;
	}
	t->opts = view_opts;
	t->nopts = n;
	t->loaded = view_loaded;
	t->nloaded = nl;
}

static void
view_of(const Config *c, Scheme *s, TmTheme *t)
{
	theme_scheme(c, s);
	themer_view(c, s, t);
}

/* ---- loading, unloading, and reloading ---- */

/* Every module's reload hook. A change to one module can affect another's files
 * (a launcher is named in the sway config), so all running programs are told. */
static void
reload_all(void)
{
	for (int i = 0; i < nreg; i++)
		if (reg[i].m->reload)
			reg[i].m->reload();
}

/* records the new state, then loads or unloads the module right away */
int
themer_module_set(Config *c, int i, int on)
{
	Scheme s;
	TmTheme t;
	int r;

	config_set_module(c, reg[i].m->name, on);
	view_of(c, &s, &t);
	if (on && reg[i].m->present && !reg[i].m->present(&t))
		on = 0;                /* recorded as on; loads once the program appears */
	r = on ? mod_load(reg[i].m, &t) : mod_unload(reg[i].m);
	reload_all();
	return r;
}

/* Sets a module option. Returns -1 if the value is not valid for the option, and
 * -2 if the module does not load with it: then the previous value is put back and
 * loaded again, so a bad value never leaves the module's files removed. */
int
themer_option_set(Config *c, int i, int j, const char *value)
{
	const TmModule *m = reg[i].m;
	char key[2 * NAME_LEN], prev[NAME_LEN];

	if (!option_valid(&m->options[j], value))
		return -1;
	option_key(key, sizeof key, i, j);
	snprintf(prev, sizeof prev, "%s", themer_option_value(c, i, j));
	config_set_option(c, key, value);
	if (themer_module_on(c, i)) {
		Scheme s;
		TmTheme t;

		view_of(c, &s, &t);
		if (mod_load(m, &t) < 0) {
			config_set_option(c, key, prev);
			view_of(c, &s, &t);
			mod_load(m, &t);
			return -2;
		}
		reload_all();
	}
	return 0;
}

/* the built-in targets, then every module: loaded if on, unloaded if off */
int
themer_apply(const Config *c)
{
	static const Target targets[] = {
		{ "env",  gen_env },
		{ "tty",  gen_tty },
		{ "grub", gen_grub },
	};
	Scheme s;
	TmTheme t;
	int fails = 0;

	view_of(c, &s, &t);
	for (size_t i = 0; i < TM_COUNT(targets); i++)
		if (targets[i].fn(c, &s) < 0) {
			fprintf(stderr, "themer: %s failed\n", targets[i].name);
			fails++;
		}
	for (int i = 0; i < nreg; i++) {
		if (themer_module_on(c, i)) {
			/* an absent program: nothing to write, and nothing left from before */
			if (reg[i].m->present && !reg[i].m->present(&t)) {
				mod_unload(reg[i].m);
				continue;
			}
			if (mod_load(reg[i].m, &t) < 0) {
				fprintf(stderr, "themer: module %s failed\n", reg[i].m->name);
				fails++;
			}
		} else {
			mod_unload(reg[i].m);      /* removes files left by an earlier load */
		}
	}
	reload_all();
	return fails;
}
