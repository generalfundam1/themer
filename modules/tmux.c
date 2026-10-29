/* tmux.c: tmux's colors: the status line, pane borders, messages, copy mode.
 *
 * Writes ~/.config/themer/tmux/themer.conf, rendered from templates/<set>/tmux/
 * themer.conf, and sources it from a marked block at the end of the user's tmux
 * config: ~/.config/tmux/tmux.conf, or ~/.tmux.conf (which tmux reads instead when it
 * exists); ~/.tmux.conf is created when there is neither. A running tmux server
 * sources the file again on every apply; options the theme stops setting keep their
 * value until the server restarts. Themer Default keeps tmux's own look, its status
 * line in the accent where tmux's is green; Themer Chicago draws a grey taskbar
 * with the current window raised in the accent. tmux has no font of its own.
 *
 * Options (after the host's mode):
 *   template  the set it is drawn from */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "themer_module.h"

static const TmOption opts[] = {
	TM_OPT_TEMPLATE,
};

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("tmux");
}

/* the user's config: ~/.tmux.conf when it exists (tmux reads it first), else the XDG
 * one when that exists, else a new ~/.tmux.conf */
static void
config_path(const TmTheme *t, char *p, size_t n)
{
	tm_path(p, n, t, ".tmux.conf");
	if (!access(p, F_OK))
		return;
	tm_path(p, n, t, ".config/tmux/tmux.conf");
	if (access(p, F_OK))
		tm_path(p, n, t, ".tmux.conf");
}

static int
load(const TmTheme *t)
{
	char body[8192], p[PATH_MAX];

	if (tm_render(body, sizeof body, "tmux", tm_opt(t, "tmux.template"), "themer.conf", t, NULL, 0) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/themer/tmux/themer.conf");
	if (tm_writef(p, "%s", body) < 0)
		return -1;
	config_path(t, p, sizeof p);
	return tm_blockf(p, "tmux", "#", "", 0, "source-file -q ~/.config/themer/tmux/themer.conf");
}

/* a running server takes the new colors; without one the command does nothing */
static void
reload(void)
{
	const char *home = getenv("HOME");
	char p[PATH_MAX];
	const char *argv[] = { "tmux", "source-file", "-q", p, NULL };

	if (!home || !tm_have("tmux"))
		return;
	snprintf(p, sizeof p, "%s/.config/themer/tmux/themer.conf", home);
	if (!access(p, R_OK))
		tm_run(argv);
}

static const TmModule mod = {
	.name = "tmux",
	.desc = "tmux: the status line, pane borders, messages and copy mode in the theme's colors",
	.templates = "tmux",
	.default_on = 1,
	.present = present,
	.load = load,
	.reload = reload,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
