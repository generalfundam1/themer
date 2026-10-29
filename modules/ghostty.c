/* ghostty.c: Ghostty's colors and font.
 *
 * Writes ~/.config/ghostty/themer, rendered from templates/<set>/ghostty/themer, and
 * reads it from a marked block at the end of the user's config ("config-file =
 * ?themer"). Ghostty reads config.ghostty and then config, and a config-file after the
 * file that names it: the block goes in config when there is one, else in
 * config.ghostty, and config is created when there is neither (every Ghostty reads
 * it). So the theme's colors and font win over the same keys in the user's config. Ghostty 1.2 and later re-read their config on SIGUSR2; an older Ghostty
 * would quit on it, so its version is asked first.
 *
 * Options (after the host's mode):
 *   font      terminal (the default) or ui
 *   template  the set it is drawn from */
#include <dirent.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "themer_module.h"

static const TmOption opts[] = {
	{ "font", "font for the terminal", "terminal|ui", "terminal", 0, 0 },
	TM_OPT_TEMPLATE,
};

static int
template_vars(const TmTheme *t, TmVar *v, int max)
{
	static char fam[128], size[16];
	int sz;

	if (max < 2)
		return 0;
	tm_font_for(t, "ghostty.font", fam, sizeof fam, &sz);
	snprintf(size, sizeof size, "%d", sz);
	v[0] = (TmVar){ "font_family", fam };
	v[1] = (TmVar){ "font_size", size };
	return 2;
}

TM_VARS(template_vars)

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("ghostty") || tm_have_flatpak("com.mitchellh.ghostty");
}

static int
load(const TmTheme *t)
{
	char body[8192], p[PATH_MAX];
	TmVar v[2];
	int nv = template_vars(t, v, TM_COUNT(v));

	if (tm_render(body, sizeof body, "ghostty", tm_opt(t, "ghostty.template"), "themer", t, v, nv) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/ghostty/themer");
	if (tm_writef(p, "%s", body) < 0)
		return -1;
	/* Ghostty reads config.ghostty, then config: the block goes in the last of them */
	tm_path(p, sizeof p, t, ".config/ghostty/config");
	if (access(p, F_OK)) {
		tm_path(p, sizeof p, t, ".config/ghostty/config.ghostty");
		if (access(p, F_OK))
			tm_path(p, sizeof p, t, ".config/ghostty/config");
	}
	return tm_blockf(p, "ghostty", "#", "", 0, "config-file = ?themer");
}

/* Ghostty 1.2 or later, which reloads on SIGUSR2 rather than quitting */
static int
reloads_on_usr2(void)
{
	char line[128];
	int major = 0, minor = 0, ok = 0;
	FILE *f;

	if (!tm_have("ghostty") || !(f = popen("ghostty +version 2>/dev/null", "r")))
		return 0;
	while (!ok && fgets(line, sizeof line, f))
		if (sscanf(line, "Ghostty %d.%d", &major, &minor) == 2)
			ok = major > 1 || (major == 1 && minor >= 2);
	pclose(f);
	return ok;
}

/* whether one of the user's processes is named name */
static int
running(const char *name)
{
	char path[64], comm[32];
	struct dirent *e;
	DIR *d = opendir("/proc");
	int found = 0;

	if (!d)
		return 0;
	while (!found && (e = readdir(d))) {
		FILE *f;

		if (e->d_name[0] < '0' || e->d_name[0] > '9')
			continue;
		snprintf(path, sizeof path, "/proc/%.20s/comm", e->d_name);
		if (!(f = fopen(path, "r")))
			continue;
		if (fgets(comm, sizeof comm, f))
			found = !strncmp(comm, name, strlen(name)) && comm[strlen(name)] == '\n';
		fclose(f);
	}
	closedir(d);
	return found;
}

static void
reload(void)
{
	if (running("ghostty") && reloads_on_usr2())
		tm_signal("ghostty", SIGUSR2);
}

static const TmModule mod = {
	.name = "ghostty",
	.desc = "Ghostty: the terminal's colors and font (themer, read from Ghostty's config)",
	.templates = "ghostty",
	.default_on = 1,
	.present = present,
	.load = load,
	.reload = reload,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
