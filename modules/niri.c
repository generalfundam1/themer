/* niri.c: niri's focus ring, window borders, insert hint and backgrounds.
 *
 * Writes ~/.config/niri/themer.kdl from templates/<set>/niri/themer.kdl and includes it
 * from the end of the user's ~/.config/niri/config.kdl:
 *   include "themer.kdl"
 * Included files merge into the config and win over what comes before them, so only
 * the colors named in themer.kdl change; widths, gaps, whether the ring or the border
 * is on, all stay the user's. niri re-reads its config, included files too, when one
 * changes, so there is nothing to reload. Includes came with niri 25.11; on an older
 * niri the module writes nothing and says so (an include line would be an error
 * there). Without a config.kdl niri runs on its built-in defaults and writes none,
 * so the module does not create one either.
 *
 * Options:
 *   template  the set under templates/niri/ */
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "themer_module.h"

static const TmOption opts[] = {
	TM_OPT_TEMPLATE,
};

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("niri");
}

/* whether this niri reads include lines (25.11 and later); a version it cannot read
 * counts as new */
static int
has_include(void)
{
	char line[128];
	int major = 0, minor = 0, ok = 1;
	FILE *p = popen("niri --version 2>/dev/null", "r");

	if (!p)
		return 1;
	if (fgets(line, sizeof line, p) && sscanf(line, "niri %d.%d", &major, &minor) == 2)
		ok = major > 25 || (major == 25 && minor >= 11);
	pclose(p);
	return ok;
}

static int
load(const TmTheme *t)
{
	char body[8192], cfg[PATH_MAX], out[PATH_MAX];

	tm_path(cfg, sizeof cfg, t, ".config/niri/config.kdl");
	if (access(cfg, F_OK)) {
		fprintf(stderr, "themer: niri: no ~/.config/niri/config.kdl (niri runs on its defaults); "
		        "nothing to hook into\n");
		return 0;
	}
	if (!has_include()) {
		fprintf(stderr, "themer: niri: this niri is older than 25.11 and cannot include files; "
		        "not themed\n");
		return 0;
	}
	if (tm_render(body, sizeof body, "niri", tm_opt(t, "niri.template"), "themer.kdl", t, NULL, 0) < 0)
		return -1;
	tm_path(out, sizeof out, t, ".config/niri/themer.kdl");
	if (tm_writef(out, "%s", body) < 0)
		return -1;
	return tm_blockf(cfg, "niri", "//", "", 0, "include \"themer.kdl\"\n");
}

static const TmModule mod = {
	.name = "niri",
	.desc = "niri: the focus ring, window borders, the insert hint and the backgrounds",
	.templates = "niri",
	.default_on = 1,
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
