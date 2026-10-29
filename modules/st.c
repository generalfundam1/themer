/* st.c: st has no runtime config; its colors are compiled in.
 * This module writes a colorname[] header from templates/<set>/st/colors.h. To use
 * it, edit st's config.h once: delete its colorname[] array and add
 *   #include "themer-colors.h"
 * in its place. The header goes to ~/.config/themer/st/ and, if ST_SRC is set, to
 * $ST_SRC. Rebuild st after applying a change. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "themer_module.h"

static const TmOption opts[] = {
	TM_OPT_TEMPLATE,
};

static int
load(const TmTheme *t)
{
	char body[4096], p[PATH_MAX];
	const char *src = getenv("ST_SRC");

	if (tm_render(body, sizeof body, "st", tm_opt(t, "st.template"), "colors.h", t, NULL, 0) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/themer/st/colors.h");
	if (tm_writef(p, "%s", body) < 0)
		return -1;
	if (src && *src) {
		snprintf(p, sizeof p, "%s/themer-colors.h", src);
		return tm_writef(p, "%s", body);
	}
	return 0;
}

/* st itself (a build of it on PATH) */
static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("st");
}

static const TmModule mod = {
	.name = "st",
	.desc = "st: a colors header (st's config.h needs one edit, see modules/st.c)",
	.templates = "st",
	.default_on = 0,
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
