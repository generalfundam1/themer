/* river.c: river's background and window border colors (river-classic, the 0.3
 * line, configured through riverctl).
 *
 * river's config is its init, an executable run at start that calls riverctl. The
 * module writes the theme's riverctl calls to ~/.config/river/themer (a shell file,
 * from templates/<set>/river/themer) and runs it from the end of the user's init:
 *   [ -f "$HOME/.config/river/themer" ] && . "$HOME/.config/river/themer"
 * That works for an init written in sh, bash, zsh or dash, the usual case; an init
 * in another language is left alone with a message (call the file from it yourself).
 * A running river takes the colors at once: reload() runs the file, inside a river
 * session. river 0.4, which hands window management to a separate program, has no
 * riverctl; this module needs it.
 *
 * Options:
 *   template  the set under templates/river/ */
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
	return tm_have("riverctl");
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

	tm_path(init, sizeof init, t, ".config/river/init");
	tm_path(script, sizeof script, t, ".config/river/themer");
	if (tm_render(body, sizeof body, "river", tm_opt(t, "river.template"), "themer", t, NULL, 0) < 0 ||
	    tm_writef(script, "%s", body) < 0)
		return -1;
	if (access(init, F_OK)) {
		fprintf(stderr, "themer: river: no ~/.config/river/init; run %s from yours\n", script);
		return 0;
	}
	if (!shell_init(init)) {
		fprintf(stderr, "themer: river: your init is not a shell script; run %s from it\n", script);
		return 0;
	}
	return tm_blockf(init, "river", "#", "", 0,
	                 "[ -f \"$HOME/.config/river/themer\" ] && . \"$HOME/.config/river/themer\"\n");
}

/* in a running river session: its Wayland display is set and river is running */
static void
reload(void)
{
	const char *argv[] = { "sh", script, NULL };

	if (script[0] && getenv("WAYLAND_DISPLAY") && (tm_signal("river", 0) || tm_signal("river-classic", 0)) &&
	    !access(script, R_OK))
		tm_run(argv);
}

static const TmModule mod = {
	.name = "river",
	.desc = "river (riverctl): the background and the window borders",
	.templates = "river",
	.default_on = 1,
	.present = present,
	.load = load,
	.reload = reload,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
