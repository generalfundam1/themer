/* front.c: what every front end does the same way. The command line, the TUI,
 * themer-gtk, and themer-qt all change the configuration through these, so a
 * setting is validated and applied identically whichever one was used: the TUI is
 * the GUI without the previews, and the CLI is both without the screen. */
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
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

/* a template set as people read it: its set.conf name= (Themer Default), or the
 * directory name when it has none. The directory name (themer_default) is what
 * files, options and the state hold. */
const char *
front_set_title(const char *set, char *out, size_t n)
{
	if (set_info(set, "name", out, n) != 0 || !out[0])
		snprintf(out, n, "%s", set);
	return out;
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

	char copy[sizeof c->ui_font];

	if (!valid_text(family, mono ? sizeof c->mono_font : sizeof c->ui_font) || size < 0 || (size && (size < FRONT_FONT_MIN || size > FRONT_FONT_MAX)))
		return -1;
	/* family may be the field itself (a size change keeps it): copy it first */
	snprintf(copy, sizeof copy, "%s", family);
	snprintf(fam, mono ? sizeof c->mono_font : sizeof c->ui_font, "%s", copy);
	if (size > 0)
		*sz = size;
	return 0;
}

int
front_set_icons(Config *c, const char *name)
{
	if (!valid_text(name, sizeof c->icons))
		return -1;
	snprintf(c->icons, sizeof c->icons, "%s", name);
	return 0;
}

int
front_set_cursor(Config *c, const char *name, int size)
{
	if (size < 0 || (size && (size < FRONT_CURSOR_MIN || size > FRONT_CURSOR_MAX)) || (name && !valid_text(name, sizeof c->cursor)))
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

	if (!strcmp(from, to) || !valid_name(to) || template_builtin(to))
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
/* ---- staying in step with the other front ends ---- */

static Config *tcfg, *tbase;

void
front_track(Config *cfg, Config *base)
{
	tcfg = cfg;
	tbase = base;
}

int
front_sync(void)
{
	static Config disk, merged;

	if (!tcfg || !state_changed())
		return 0;
	if (config_defaults(&disk) < 0 || state_load(&disk) < 0)
		return 0;
	merged = disk;
	config_merge(&merged, tcfg, tbase);
	*tbase = disk;
	*tcfg = merged;
	return 1;
}

/* the parts saved at once (modules, their options, the port) go from cfg to base,
 * which is saved: cfg's unapplied browsing stays unsaved */
int
front_save_now(Config *c)
{
	if (!tcfg || c != tcfg)
		return state_save(c);
	front_sync();
	memcpy(tbase->mods, c->mods, sizeof c->mods);
	tbase->nmods = c->nmods;
	memcpy(tbase->optv, c->optv, sizeof c->optv);
	tbase->nopt = c->nopt;
	tbase->web_port = c->web_port;
	return state_save(tbase);
}

/* an option set (or reset, value NULL) and its module reloaded, from the saved
 * settings when a front end tracks them, so the program shows the applied theme and
 * not one being browsed; the browsed settings get the same value */
static int
option_now(Config *c, int module, int option, const char *value)
{
	char key[2 * NAME_LEN];
	const char *now;
	int r;

	if (!tcfg || c != tcfg)
		return themer_option_set(c, module, option, value);
	front_sync();
	r = themer_option_set(tbase, module, option, value);
	snprintf(key, sizeof key, "%s.%s", themer_module_name(module), themer_option(module, option)->name);
	if ((now = config_option(tbase, key)))
		config_set_option(c, key, now);
	else
		config_unset_option(c, key);
	return r;
}

/* a module loaded or unloaded now, the same way: from the saved settings */
int
front_module_set(Config *c, int module, int on)
{
	int r;

	if (on && !themer_module_present(c, module))
		return -3;           /* its program is not installed */
	if (!tcfg || c != tcfg) {
		r = themer_module_set(c, module, on);
		state_save(c);
		return r;
	}
	front_sync();
	r = themer_module_set(tbase, module, on);
	config_set_module(c, themer_module_name(module), on);
	front_save_now(c);
	return r;
}

int
front_option_set(Config *c, int module, int option, const char *value, char *msg, size_t n)
{
	int r = option_now(c, module, option, value);

	if (r == -1) {
		snprintf(msg, n, "'%s' is not a valid value for %s", value, themer_option(module, option)->name);
		return -1;
	}
	if (r == -2) {
		snprintf(msg, n, "%s did not load with '%s'; the previous value was kept",
		         themer_module_name(module), value);
		return -1;
	}
	if (front_save_now(c) < 0) {
		snprintf(msg, n, "set, but the state file could not be saved");
		return -1;
	}
	snprintf(msg, n, "%s.%s = %s", themer_module_name(module), themer_option(module, option)->name, value);
	return 0;
}

int
front_option_reset(Config *c, int module, int option, char *msg, size_t n)
{
	const char *name = themer_option(module, option)->name;

	if (option_now(c, module, option, NULL) == -2) {
		snprintf(msg, n, "%s did not load with the default %s; the previous value was kept",
		         themer_module_name(module), name);
		return -1;
	}
	if (front_save_now(c) < 0) {
		snprintf(msg, n, "reset, but the state file could not be saved");
		return -1;
	}
	snprintf(msg, n, "%s.%s = %s (default)", themer_module_name(module), name,
	         themer_option_value(c, module, option));
	return 0;
}

const char *const front_option_groups[OG_N] = { "Look", "What it themes" };

int
front_option_is_switch(int module, int option)
{
	const TmOption *o = themer_option(module, option);

	return o->choices && !strcmp(o->choices, "on|off");
}

int
front_option_control(int module, int option)
{
	const TmOption *o = themer_option(module, option);

	if (front_option_is_switch(module, option))
		return OC_SWITCH;
	if (!strcmp(o->name, "template"))
		return OC_LIST;
	if (o->choices) {
		int n = 1;

		for (const char *p = o->choices; *p; p++)
			n += *p == '|';
		return n <= 4 && strlen(o->choices) <= 32 ? OC_BUTTONS : OC_LIST;
	}
	if (o->max > o->min)
		return OC_SLIDER;
	return OC_TEXT;
}

const char *
front_module_state(const Config *c, int module)
{
	return front_text(!themer_module_present(c, module) ? FT_STATE_ABSENT
	                  : themer_module_on(c, module) ? FT_STATE_ON : FT_STATE_OFF);
}

const char *
front_module_note(const Config *c, int module)
{
	if (themer_module_present(c, module))
		return NULL;
	return front_text(FT_ABSENT_NOTE);
}

void
front_option_note(const Config *c, int module, int option, char *out, size_t n)
{
	const TmOption *o = themer_option(module, option);
	char title[NAME_LEN];

	if (!themer_option_is_set(c, module, option)) {
		snprintf(out, n, "%s", o->desc);
		return;
	}
	if (!strcmp(o->name, "template"))
		snprintf(out, n, "%s \u00b7 %s: %s (%s)", o->desc, front_text(FT_DEFAULT), front_text(FT_GLOBAL_SET),
		         front_set_title(c->set, title, sizeof title));
	else
		snprintf(out, n, "%s \u00b7 %s: %s", o->desc, front_text(FT_DEFAULT), o->def ? o->def : "");
}

int
front_option_group(int module, int option)
{
	if (front_option_is_switch(module, option))
		return OG_PARTS;
	return OG_LOOK;
}

/* stylus.open=apply: after an Apply from a front end, each site that is on opens in
 * the browser, where Stylus offers to install or update it. Not from the CLI's apply,
 * toggle or theme commands, which keybindings and login scripts run. */
static int
stylus_open_on_apply(const Config *c, char *note, size_t n)
{
	int i = themer_module_find("stylus"), j;
	const char *sites[64];

	if (i < 0 || !themer_module_on(c, i) || (j = themer_option_find(i, "open")) < 0 ||
	    strcmp(themer_option_value(c, i, j), "apply") || !front_stylus_sites(c, sites, (int)TM_COUNT(sites)))
		return 0;
	return front_stylus_install(c, NULL, note, n);
}

int
front_remove(Config *c, char *msg, size_t n)
{
	int fails;

	if (tcfg && c == tcfg)
		front_sync();
	fails = themer_remove_all();
	c->removed = 1;
	if (tcfg && c == tcfg)
		tbase->removed = 1;
	if (state_save(tcfg && c == tcfg ? tbase : c) < 0)
		fails++;
	snprintf(msg, n, "%s", front_text(fails ? FT_REMOVE_FAILED : FT_REMOVED));
	return fails;
}

int
front_apply(Config *c, char *msg, size_t n)
{
	char note[160] = "";
	int fails;

	if (tcfg && c == tcfg)
		front_sync();         /* what another front end changed is applied too */
	c->removed = 0;         /* Apply puts removed styles back */
	fails = themer_apply(c);
	if (state_save(c) < 0)
		fails++;
	else if (tcfg && c == tcfg)
		*tbase = *c;
	if (!fails)
		stylus_open_on_apply(c, note, sizeof note);   /* note: what it opened, or why not */
	if (msg && n) {
		if (fails)
			snprintf(msg, n, "applied with %d failure%s: see the output", fails, fails == 1 ? "" : "s");
		else
			snprintf(msg, n, "applied and saved; running programs take it when restarted%s%s",
			         *note ? "; stylus: " : "", note);
	}
	return fails;
}

int
front_module_group(const Config *c, int module)
{
	return !themer_module_present(c, module) ? MG_ABSENT : themer_module_on(c, module) ? MG_LOADED : MG_UNLOADED;
}

const char *
front_module_group_title(int group)
{
	static const int t[MG_N] = { FT_GROUP_LOADED, FT_GROUP_UNLOADED, FT_GROUP_ABSENT };

	return group >= 0 && group < MG_N ? front_text(t[group]) : "";
}

static const Config *order_cfg;
static int order_group[MAX_MODULES];

static int
cmp_module(const void *a, const void *b)
{
	int i = *(const int *)a, j = *(const int *)b;

	if (order_group[i] != order_group[j])
		return order_group[i] - order_group[j];
	return strcmp(themer_module_name(i), themer_module_name(j));
}

/* every module: the loaded ones, then the unloaded, then those whose program is not
 * installed, each by name (front_module_group says which a module is in) */
int
front_module_order(const Config *c, int *order, int max)
{
	int n = themer_module_count() < max ? themer_module_count() : max;

	order_cfg = c;
	for (int i = 0; i < n; i++) {
		order[i] = i;
		order_group[i] = front_module_group(c, i);
	}
	qsort(order, n, sizeof order[0], cmp_module);
	return n;
}

int
front_render(const Config *c, const char *module, const char *dir, const char *file,
             char *out, size_t n)
{
	Scheme s;
	TmTheme t;
	TmVar v[TM_MAX_VARS];
	int i = module ? themer_module_find(module) : -1, nv = 0;

	if (i >= 0) {
		themer_module_view(c, i, &s, &t);
		nv = themer_module_vars(i, &t, v, (int)TM_COUNT(v));
	} else {
		theme_scheme(c, &s);
		themer_view(c, &s, &t);
	}
	if (nv < 0)
		return -1;
	return tm_render(out, n, dir, c->set, file, &t, v, nv);
}

/* the template set a module renders from: its template option, else the global set */
const char *
front_template_set(const Config *c, const char *module)
{
	int i = module ? themer_module_find(module) : -1;
	int j = i < 0 ? -1 : themer_option_find(i, "template");

	return j < 0 ? c->set : themer_option_value(c, i, j);
}

/* 1 if the set a module renders from has dir/file of its own (not by falling back
 * to the default set) */
int
front_module_has(const Config *c, const char *module, const char *dir, const char *file)
{
	char d[PATH_MAX], p[PATH_MAX];

	if (tm_template_dir(dir, front_template_set(c, module), d, sizeof d) < 0 ||
	    snprintf(p, sizeof p, "%s/%s", d, file) >= (int)sizeof p)
		return 0;
	return access(p, R_OK) == 0;
}

/* 1 if a module draws the dark side: its mode option, else the global mode */
int
front_module_dark(const Config *c, const char *module)
{
	Scheme s;
	TmTheme t;
	int i = themer_module_find(module);

	if (i < 0)
		return c->mode == MODE_DARK;
	themer_module_view(c, i, &s, &t);
	return t.dark;
}

/* ---- themer-web, the server the stylus module and the web front end use ---- */

static int serving;   /* the port this process serves on, in themer-web */

void
front_web_serving(int port)
{
	serving = port;
}

int
front_web_port(const Config *c)
{
	if (serving)
		return serving;
	return c->web_port >= 1024 && c->web_port <= 65535 ? c->web_port : 7331;
}

/* a new port, saved; a server running on the old one is moved to it */
int
front_set_web_port(Config *c, int port)
{
	int old = front_web_port(c), up;

	if (port < 1024 || port > 65535)
		return -1;
	if (port == old)
		return 0;
	up = front_web_running(old);
	c->web_port = port;
	front_save_now(c);
	if (up) {
		front_web_stop(old);
		return front_web_start(port);
	}
	return 0;
}

/* true if something listens on localhost:port */
int
front_web_running(int port)
{
	int s = socket(AF_INET, SOCK_STREAM, 0), r;
	struct sockaddr_in a;

	if (s < 0)
		return 0;
	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_port = htons((unsigned short)port);
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	r = connect(s, (struct sockaddr *)&a, sizeof a) == 0;
	close(s);
	return r;
}

/* starts themer-web detached, or stops the one on the port; 0 on success */
int
front_web_start(int port)
{
	char cmd[64];

	snprintf(cmd, sizeof cmd, "themer-web -d -p %d", port);
	if (run(cmd) != 0)
		return -1;
	for (int i = 0; i < 20 && !front_web_running(port); i++)
		usleep(50000);
	return front_web_running(port) ? 0 : -1;
}

int
front_web_stop(int port)
{
	char cmd[64];

	snprintf(cmd, sizeof cmd, "themer-web -q -p %d", port);
	return run(cmd) == 0 && !front_web_running(port) ? 0 : -1;
}

/* ---- colour schemes ---- */

/* removes a scheme, unless it is the one applied (c): yours are deleted, built-in
 * ones hidden. msg says what happened, in every front end's words. */
int
front_scheme_remove(Config *c, const char *name, char *msg, size_t n)
{
	if (!strcmp(name, c->th.name)) {
		snprintf(msg, n, "%s: %s", name, front_text(FT_SCHEME_IN_USE));
		return -1;
	}
	switch (scheme_remove(name)) {
	case 0:
		snprintf(msg, n, "%s %s", name, front_text(FT_SCHEME_DELETED));
		return 0;
	case 1:
		snprintf(msg, n, "%s %s", name, front_text(FT_SCHEME_HIDDEN));
		return 0;
	}
	snprintf(msg, n, "%s: %s", name, front_text(FT_SCHEME_REMOVE_FAILED));
	return -1;
}

/* ---- opening things in the browser ---- */

int
front_open(const char *target)
{
	pid_t pid;
	int st;

	if (!tm_have("xdg-open"))
		return -1;
	/* detached and without a shell: a browser started by xdg-open may stay in the
	 * foreground until it exits, and must not die with a TUI's terminal */
	if ((pid = fork()) < 0)
		return -1;
	if (pid == 0) {
		int null = open("/dev/null", O_RDWR);

		setsid();
		if (null >= 0) {
			dup2(null, 0);
			dup2(null, 1);
			dup2(null, 2);
		}
		if (fork() == 0) {
			execlp("xdg-open", "xdg-open", target, (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	return waitpid(pid, &st, 0) == pid && WIFEXITED(st) && !WEXITSTATUS(st) ? 0 : -1;
}

int
front_stylus_sites(const Config *c, const char **sites, int max)
{
	int i = themer_module_find("stylus"), n = 0;

	if (i < 0 || !themer_module_on(c, i))
		return 0;
	for (int j = 0; j < themer_option_count(i) && n < max; j++)
		if (front_option_group(i, j) == OG_PARTS && !strcmp(themer_option_value(c, i, j), "on"))
			sites[n++] = themer_option(i, j)->name;
	return n;
}

int
front_stylus_install(const Config *c, const char *site, char *msg, size_t n)
{
	const char *sites[64];
	int ns = front_stylus_sites(c, sites, (int)TM_COUNT(sites)), port = front_web_port(c), opened = 0;
	char url[128], file[PATH_MAX];

	if (!ns) {
		snprintf(msg, n, "turn the stylus module (and a site in it) on first");
		return -1;
	}
	if (!tm_have("xdg-open")) {
		snprintf(msg, n, "no xdg-open here: open http://localhost:%d/stylus/<site>.user.css in the browser", port);
		return -1;
	}
	if (!front_web_running(port) && front_web_start(port) < 0) {
		snprintf(msg, n, "could not start themer-web on port %d", port);
		return -1;
	}
	for (int k = 0; k < ns; k++) {
		if (site && strcmp(site, sites[k]))
			continue;
		/* not rendered yet (no apply since the site was turned on): the page would be a 404 */
		snprintf(url, sizeof url, ".config/themer/stylus/%s.user.css", sites[k]);
		if (path_under_home(file, sizeof file, url) < 0 || access(file, R_OK)) {
			snprintf(msg, n, "the %s style is not made yet: apply first", sites[k]);
			return -1;
		}
		snprintf(url, sizeof url, "http://localhost:%d/stylus/%s.user.css", port, sites[k]);
		if (front_open(url) < 0) {
			snprintf(msg, n, "xdg-open could not open %s", url);
			return -1;
		}
		opened++;
	}
	if (!opened) {
		snprintf(msg, n, "%s is not a site the stylus module has on", site);
		return -1;
	}
	snprintf(msg, n, opened == 1 ? "opened in the browser: click Install style in Stylus"
	                             : "opened %d styles in the browser: click Install style on each", opened);
	return 0;
}

/* ---- single colors over the scheme, the console font, the environment ---- */

int
front_set_color(Config *c, int role, const char *hex)
{
	char v[TM_COLOR_LEN];
	size_t i;

	if (role < 0 || role >= C_N)
		return -1;
	if (!hex || !*hex) {               /* back to the scheme's color */
		c->color[role][0] = '\0';
		return 0;
	}
	if (*hex == '#')
		hex++;
	if (strlen(hex) != 6)
		return -1;
	for (i = 0; i < 6; i++) {
		if (!isxdigit((unsigned char)hex[i]))
			return -1;
		v[i] = (char)tolower((unsigned char)hex[i]);
	}
	v[6] = '\0';
	snprintf(c->color[role], sizeof c->color[role], "%s", v);
	return 0;
}

int
front_color_role(const char *name)
{
	for (int i = 0; i < C_N; i++)
		if (!strcmp(color_names[i], name))
			return i;
	return -1;
}

int
front_set_tty_font(Config *c, const char *name)
{
	if (!name || !valid_name(name) || strlen(name) >= sizeof c->tty_font)
		return -1;
	snprintf(c->tty_font, sizeof c->tty_font, "%s", name);
	return 0;
}

static int
cmp_name(const void *a, const void *b)
{
	return strcmp((const char *)a, (const char *)b);
}

int
front_tty_fonts(char names[][NAME_LEN], int max)
{
	static const char *const dirs[] = {
		"/usr/share/kbd/consolefonts", "/usr/share/consolefonts", "/usr/lib/kbd/consolefonts",
	};
	int n = 0, j = 0;

	for (size_t d = 0; d < TM_COUNT(dirs); d++) {
		DIR *dp = opendir(dirs[d]);
		struct dirent *e;

		if (!dp)
			continue;
		while ((e = readdir(dp)) && n < max) {
			char *dot;

			if (e->d_name[0] == '.')
				continue;
			if (strlen(e->d_name) >= NAME_LEN)
				continue;
			memcpy(names[n], e->d_name, strlen(e->d_name) + 1);
			if ((dot = strchr(names[n], '.')))   /* ter-v16n.psf.gz -> ter-v16n */
				*dot = '\0';
			if (valid_name(names[n]))
				n++;
		}
		closedir(dp);
	}
	qsort(names, (size_t)n, NAME_LEN, cmp_name);
	for (int i = 0; i < n; i++)
		if (!j || strcmp(names[i], names[j - 1]))
			memmove(names[j++], names[i], NAME_LEN);
	return j;
}

int
front_env_set(Config *c, const char *name, const char *value, char *msg, size_t n)
{
	size_t k = name ? strlen(name) : 0;

	if (!k || k >= sizeof c->env[0].name || !(isalpha((unsigned char)*name) || *name == '_')) {
		snprintf(msg, n, "a variable name is letters, digits and _, not starting with a digit");
		return -1;
	}
	for (const char *p = name; *p; p++)
		if (!isalnum((unsigned char)*p) && *p != '_') {
			snprintf(msg, n, "a variable name is letters, digits and _, not starting with a digit");
			return -1;
		}
	if (!value)
		value = "";
	if (*value && !valid_text(value, sizeof c->env[0].value)) {
		snprintf(msg, n, "the value must be one line, shorter than %zu characters", sizeof c->env[0].value);
		return -1;
	}
	if (*value && c->nenv == MAX_ENV) {
		int found = 0;

		for (int i = 0; i < c->nenv; i++)
			found |= !strcmp(c->env[i].name, name);
		if (!found) {
			snprintf(msg, n, "at most %d variables", MAX_ENV);
			return -1;
		}
	}
	env_set(c, name, value);
	snprintf(msg, n, *value ? "%s set" : "%s removed", name);
	return 0;
}

/* ---- an action beside an option: Install, for a stylus site that is on ---- */

const char *
front_option_action(const Config *c, int module, int option)
{
	if (strcmp(themer_module_name(module), "stylus") || front_option_group(module, option) != OG_PARTS ||
	    !themer_module_on(c, module) || strcmp(themer_option_value(c, module, option), "on"))
		return NULL;
	return "Install";
}

int
front_option_link(const Config *c, int module, int option, char *out, size_t n)
{
	if (!front_option_action(c, module, option))
		return -1;
	return snprintf(out, n, "/stylus/%s.user.css", themer_option(module, option)->name) < (int)n ? 0 : -1;
}

int
front_option_run(const Config *c, int module, int option, char *msg, size_t n)
{
	if (!front_option_action(c, module, option)) {
		snprintf(msg, n, "nothing to do for that option");
		return -1;
	}
	return front_stylus_install(c, themer_option(module, option)->name, msg, n);
}
