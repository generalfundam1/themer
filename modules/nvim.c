/* nvim.c: a colorscheme named "themer", plus a block that selects it.
 * The colorscheme is templates/<set>/nvim/themer.lua. The selecting block goes at the
 * end of init.lua (select.lua) or init.vim (select.vim), so it runs after the user's
 * setup. A user's init.vim is kept as vimscript; otherwise init.lua is used. */
#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include "themer_module.h"

static const TmOption opts[] = {
	TM_OPT_TEMPLATE,
};

/* nvim or vim, or a config for either */
static int
present(const TmTheme *t)
{
	char lua[PATH_MAX], vim[PATH_MAX];

	tm_path(lua, sizeof lua, t, ".config/nvim/init.lua");
	tm_path(vim, sizeof vim, t, ".config/nvim/init.vim");
	return !access(lua, F_OK) || !access(vim, F_OK) || tm_have("nvim");
}

static int
load(const TmTheme *t)
{
	const char *set = tm_opt(t, "nvim.template");
	char p[PATH_MAX], lua[PATH_MAX], vim[PATH_MAX], body[8192];
	int use_vim;

	if (tm_render(body, sizeof body, "nvim", set, "themer.lua", t, NULL, 0) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/nvim/colors/themer.lua");
	if (tm_writef(p, "%s", body) < 0)
		return -1;

	tm_path(lua, sizeof lua, t, ".config/nvim/init.lua");
	tm_path(vim, sizeof vim, t, ".config/nvim/init.vim");
	use_vim = access(lua, F_OK) != 0 && access(vim, F_OK) == 0;

	if (use_vim) {
		if (tm_render(body, sizeof body, "nvim", set, "select.vim", t, NULL, 0) < 0)
			return -1;
		return tm_blockf(vim, "nvim", "\"", "", 0, "%s", body);
	}
	if (tm_render(body, sizeof body, "nvim", set, "select.lua", t, NULL, 0) < 0)
		return -1;
	return tm_blockf(lua, "nvim", "--", "", 0, "%s", body);
}

static const TmModule mod = {
	.name = "nvim",
	.desc = "Neovim: the themer colorscheme, selected from init.lua or init.vim",
	.templates = "nvim",
	.default_on = 1,
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
