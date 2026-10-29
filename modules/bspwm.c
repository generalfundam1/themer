/* bspwm.c: bspwm's window border and preselection colors.
 *
 * bspwm's config is bspwmrc, a shell script run at start that calls bspc. The module
 * writes the theme's bspc calls to ~/.config/bspwm/themer (from
 * templates/<set>/bspwm/themer) and runs it from the end of the user's bspwmrc:
 *   [ -f "$HOME/.config/bspwm/themer" ] && . "$HOME/.config/bspwm/themer"
 * That works for a bspwmrc written in sh, bash, zsh or dash, the usual case; one in
 * another language is left alone with a message (call the file from it yourself). A
 * running bspwm takes the colors at once: reload() runs the file while bspwm runs.
 *
 * Options:
 *   template  the set under templates/bspwm/ */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "themer_module.h"

static const TmOption opts[] = {
	TM_OPT_TEMPLATE,
};

static char script[PATH_MAX];   /* the file reload() runs */

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("bspwm") && tm_have("bspc");
}

/* whether the init is a shell script: no #! line, or one naming a Bourne-like shell */
static int
shell_init(const char *path)
{
	static const char *const shells[] = { "/sh", "/bash", "/zsh", "/dash", "/ksh", "/mksh", " sh", " bash", " zsh", " dash" };
	char line[256];
	int ok = 1;
	FILE *f = fopen(path, "r");

	if (!f)
		return 1;
	if (fgets(line, sizeof line, f) && !strncmp(line, "#!", 2)) {
		line[strcspn(line, "\r\n")] = '\0';
		ok = 0;
		for (size_t i = 0; i < TM_COUNT(shells); i++) {
			const char *s = strstr(line, shells[i]);
			size_t n = strlen(shells[i]);

			if (s && (s[n] == '\0' || s[n] == ' '))
				ok = 1;
		}
	}
	fclose(f);
	return ok;
}

static int
load(const TmTheme *t)
{
	char body[4096], init[PATH_MAX];

	tm_path(init, sizeof init, t, ".config/bspwm/bspwmrc");
	tm_path(script, sizeof script, t, ".config/bspwm/themer");
	if (tm_render(body, sizeof body, "bspwm", tm_opt(t, "bspwm.template"), "themer", t, NULL, 0) < 0 ||
	    tm_writef(script, "%s", body) < 0)
		return -1;
	if (access(init, F_OK)) {
		fprintf(stderr, "themer: bspwm: no ~/.config/bspwm/bspwmrc; run %s from yours\n", script);
		return 0;
	}
	if (!shell_init(init)) {
		fprintf(stderr, "themer: bspwm: your bspwmrc is not a shell script; run %s from it\n", script);
		return 0;
	}
	return tm_blockf(init, "bspwm", "#", "", 0,
	                 "[ -f \"$HOME/.config/bspwm/themer\" ] && . \"$HOME/.config/bspwm/themer\"\n");
}

/* while bspwm runs (bspc talks to it through its socket) */
static void
reload(void)
{
	const char *argv[] = { "sh", script, NULL };

	if (script[0] && tm_signal("bspwm", 0) && !access(script, R_OK))
		tm_run(argv);
}

static const TmModule mod = {
	.name = "bspwm",
	.desc = "bspwm: the window borders and the preselection area",
	.templates = "bspwm",
	.default_on = 1,
	.present = present,
	.load = load,
	.reload = reload,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
