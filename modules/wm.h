/* wm.h: the writer for sway and i3 configs, shared by modules/sway.c and
 * modules/i3.c. The two use the same config syntax, so one writer, one options
 * table, and one template directory ("wm") serve both. Each module passes its
 * own name; its options are read as "<name>.<option>".
 *
 * Writes <name>'s themer.conf next to the user's config and includes it from
 * the end of that config. Window decorations follow the style contract: the
 * focused window or tab takes the accent, the rest sit on the surface color,
 * and every border is an outline.
 *
 * Options:
 *   font      ui or terminal, for window titles and the bar
 *   menu      the launcher bound to $menu: auto, wmenu, dmenu, or none
 *   bar       a themed bar block: auto adds one only if the config has none
 *   template  the set under templates/wm/
 * Borders, gaps, and titlebars are the user's own config: themer writes colors and
 * the font, nothing a line in the config could not say. */
#ifndef WM_H
#define WM_H

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "themer_module.h"

#define WM_TEMPLATES "wm"

static const TmOption wm_options[] = {
	{ "font",       "font for window titles and the bar", "ui|terminal", "ui", 0, 0 },
	{ "menu",       "launcher for $menu (wmenu, dmenu, or none)", "auto|wmenu|dmenu|none", "auto", 0, 0 },
	{ "bar",        "themed bar block: auto adds one only if the config has none", "auto|on|off", "auto", 0, 0 },
	TM_OPT_TEMPLATE,
};

/* "<wm>.<option>", in a static buffer */
static const char *
wm_key(const char *wm, const char *option)
{
	static char key[64];

	snprintf(key, sizeof key, "%s.%s", wm, option);
	return key;
}

static const char *
wm_opt(const TmTheme *t, const char *wm, const char *option)
{
	return tm_opt(t, wm_key(wm, option));
}

/* the user's config file for a window manager: ~/.config/<wm>/config */
static void
wm_config_path(const TmTheme *t, const char *wm, char *out, size_t n)
{
	char rel[128];

	snprintf(rel, sizeof rel, ".config/%s/config", wm);
	tm_path(out, n, t, rel);
}

/* the module is used only where its window manager is in use */
static int
wm_present(const TmTheme *t, const char *wm, const char *socket_var)
{
	char p[PATH_MAX];

	/* sway sets I3SOCK as well: its socket says nothing about i3 */
	if (getenv(socket_var) && !(!strcmp(socket_var, "I3SOCK") && getenv("SWAYSOCK")))
		return 1;
	wm_config_path(t, wm, p, sizeof p);
	return !access(p, F_OK);
}

/* true if the file has a "bar {" block of its own */
static int
wm_has_bar(const char *path)
{
	char *line = NULL;
	size_t cap = 0;
	int found = 0;
	FILE *f = fopen(path, "r");

	if (!f)
		return 0;
	while (getline(&line, &cap, f) > 0) {
		const char *s = line;

		while (*s == ' ' || *s == '\t')
			s++;
		if (!strncmp(s, "bar", 3) && (s[3] == ' ' || s[3] == '{' || s[3] == '\t'))
			found = 1;
	}
	free(line);
	fclose(f);
	return found;
}

/* The launcher script for $menu: the chosen menu's, if its module is loaded, or
 * "" for none. auto takes the first loaded menu whose binary is installed. */
static void
wm_menu_script(const TmTheme *t, const char *wm, char *out, size_t n)
{
	const char *choice = wm_opt(t, wm, "menu");

	out[0] = '\0';
	if (!choice || !strcmp(choice, "auto"))
		choice = tm_loaded(t, "wmenu") && tm_have("wmenu") ? "wmenu"
		       : tm_loaded(t, "dmenu") && tm_have("dmenu") ? "dmenu" : "none";
	if (strcmp(choice, "wmenu") && strcmp(choice, "dmenu"))
		return;
	if (!tm_loaded(t, choice))
		return;
	snprintf(out, n, "%s/.config/themer/bin/themer-%s", t->home, choice);
}

/* The window manager's own variables for its templates: the font, the bar block
 * (empty unless the bar is wanted), and the cursor line. Returns the count, or -1 if
 * the bar block cannot be rendered. */
static int
wm_vars(const TmTheme *t, const char *wm, TmVar *v, int max)
{
	static char bar[2048], fam[128], size_s[16], cursor[256];
	const char *bar_opt = wm_opt(t, wm, "bar");
	TmVar bv[2];
	int size, want_bar;

	if (max < 4)
		return 0;
	tm_font_for(t, wm_key(wm, "font"), fam, sizeof fam, &size);
	snprintf(size_s, sizeof size_s, "%d", size);
	if (!bar_opt || !strcmp(bar_opt, "auto")) {
		char cfg[PATH_MAX];

		wm_config_path(t, wm, cfg, sizeof cfg);
		want_bar = !wm_has_bar(cfg);
	} else {
		want_bar = !strcmp(bar_opt, "on");
	}

	bar[0] = '\0';
	bv[0] = (TmVar){ "font_family", fam };
	bv[1] = (TmVar){ "font_size", size_s };
	if (want_bar && tm_render(bar, sizeof bar, WM_TEMPLATES, wm_opt(t, wm, "template"),
	                          "bar.conf", t, bv, 2) < 0)
		return -1;
	v[0] = (TmVar){ "font_family", fam };
	v[1] = (TmVar){ "font_size", size_s };
	v[2] = (TmVar){ "bar_block", bar };
	/* sway sets the cursor on its seat; i3 gets it from Xresources (cursor module) */
	cursor[0] = '\0';
	if (!strcmp(wm, "sway"))
		snprintf(cursor, sizeof cursor, "seat * xcursor_theme %s %d", t->cursor, t->cursor_size);
	v[3] = (TmVar){ "cursor_block", cursor };
	return 4;
}

/* writes <wm>/themer.conf from templates/wm/<set>/themer.conf and includes it from
 * <wm>/config. The bar block comes from bar.conf, through wm_vars. */
static int
wm_load(const TmTheme *t, const char *wm, const char *socket_var)
{
	char cfg[PATH_MAX], conf[PATH_MAX], rel[128], script[PATH_MAX], menu_line[PATH_MAX + 32];
	char body[4096];
	TmVar v[4];
	int nv;

	if (!wm_present(t, wm, socket_var))
		return 0;
	wm_config_path(t, wm, cfg, sizeof cfg);
	snprintf(rel, sizeof rel, ".config/%s/themer.conf", wm);
	tm_path(conf, sizeof conf, t, rel);

	nv = wm_vars(t, wm, v, TM_COUNT(v));
	if (nv < 0 || tm_render(body, sizeof body, WM_TEMPLATES, wm_opt(t, wm, "template"),
	                        "themer.conf", t, v, nv) < 0)
		return -1;
	if (tm_writef(conf, "%s", body) < 0)
		return -1;

	/* $menu is expanded where a binding is read, so it must be set in the config
	 * before the bindings that use it. Replace the user's own set $menu line in
	 * place; if there is none, put one at the top of the file. */
	wm_menu_script(t, wm, script, sizeof script);
	if (script[0]) {
		int r;

		snprintf(menu_line, sizeof menu_line, "set $menu %s", script);
		r = tm_setline(cfg, "set $menu", menu_line);
		if (r < 0)
			return -1;
		if (r == 1 && tm_blockf(cfg, "wm-menu", "#", "", 1, "%s\n", menu_line) < 0)
			return -1;
	}

	return tm_blockf(cfg, "wm", "#", "", 0, "include %s\n", conf);
}

/* reloads the running window manager; its socket variable is set only inside its
 * own session, so nothing runs elsewhere */
static void
wm_reload(const char *socket_var, const char *cmd)
{
	char prog[64];

	/* sway sets I3SOCK as well as SWAYSOCK: under sway, i3 is not running */
	if (!strcmp(socket_var, "I3SOCK") && getenv("SWAYSOCK"))
		return;
	snprintf(prog, sizeof prog, "%.*s", (int)strcspn(cmd, " "), cmd);
	if (getenv(socket_var) && tm_have(prog) && system(cmd) != 0)
		fprintf(stderr, "themer: %s failed\n", cmd);
}

#endif
