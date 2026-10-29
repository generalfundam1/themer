/* main.c: the command line. Every command loads the state, acts, and saves it;
 * the TUI saves on its own. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "themer.h"

static void
usage(void)
{
	fputs("usage: themer [tui | apply | remove | restore | toggle | dark | light | theme NAME |\n"
	      "               list | check |\n"
	      "               icons [NAME] | cursor [NAME [SIZE]] | font ui|mono FAMILY [SIZE] |\n"
	      "               font tty [NAME] | color [ROLE HEX|reset] | env [NAME=VALUE] |\n"
	      "               render [hinting none|slight|medium|full] [antialias on|off]\n"
	      "                      [rgba none|rgb|bgr|vrgb|vbgr] |\n"
	      "               set | set NAME | set list | set copy FROM TO |\n"
	      "               set import NAME MODULE PATH | set check NAME |\n"
	      "               module list | module load|unload NAME |\n"
	      "               module options NAME | module option NAME KEY VALUE |\n"
	      "               module reset NAME KEY | server start|stop|status | server port N |\n"
	      "               server install [SITE] |\n"
	      "               scheme import FILE [NAME] | scheme remove NAME | scheme restore [NAME] |\n"
	      "               template list DIR [SET] | template render MODULE SET FILE [OUT] |\n"
	      "               system install|remove|sync|watch|status [USER] | -v]\n", stderr);
}

static int
no_module(const char *name)
{
	fprintf(stderr, "themer: no module '%s'\n", name);
	return 1;
}

/* themer check: every theme, with the colors the readability rules move */
static int
cmd_check(void)
{
	static const char *mn[2] = { "dark", "light" };
	static char names[MAX_THEMES][NAME_LEN];
	int n = theme_list(names, MAX_THEMES), bad = 0;

	for (int i = 0; i < n; i++) {
		Theme before, after;
		int adjusted = 0;

		if (theme_read(names[i], &before) < 0) {
			printf("%-14s cannot be read\n", names[i]);
			bad++;
			continue;
		}
		after = before;
		theme_enforce(&after, &adjusted);
		printf("%-14s %s\n", names[i], adjusted ? "" : "ok");
		for (int m = 0; m < 2; m++) {
			for (int k = 0; k < C_N; k++)
				if (strcmp(before.hex[m][k], after.hex[m][k]))
					printf("    %-5s %-6s %s -> %s\n", mn[m], color_names[k],
					       before.hex[m][k], after.hex[m][k]);
			for (int k = 0; k < 16; k++)
				if (strcmp(before.ansi[m][k], after.ansi[m][k]))
					printf("    %-5s ansi%-2d %s -> %s\n", mn[m], k,
					       before.ansi[m][k], after.ansi[m][k]);
		}
	}
	return bad ? 1 : 0;
}

/* themer module options NAME: by section, as the front ends show them; a value the
 * user set is marked with *, the rest are defaults */
static int
cmd_module_options(const Config *c, int i)
{
	int n = themer_option_count(i);

	if (!n) {
		printf("%s has no options\n", themer_module_name(i));
		return 0;
	}
	for (int g = 0; g < OG_N; g++) {
		int head = 0;

		for (int j = 0; j < n; j++) {
			const TmOption *o = themer_option(i, j);

			if (front_option_group(i, j) != g)
				continue;
			if (!head++)
				printf("# %s\n", front_option_groups[g]);
			printf("%-12s = %-14s %s %s", o->name, themer_option_value(c, i, j),
			       themer_option_is_set(c, i, j) ? "*" : " ", o->desc);
			if (o->choices)
				printf("  [%s]", o->choices);
			else if (o->max > o->min)
				printf("  [%d-%d]", o->min, o->max);
			printf("\n");
		}
	}
	return 0;
}

/* themer set list: every template set, with the template directories it covers; the
 * current one is marked. A set covers what it has files for; the rest comes from
 * the built-in set. */
static int
cmd_set_list(const Config *c)
{
	char sets[MAX_NAMES][NAME_LEN], dirs[MAX_NAMES][NAME_LEN];
	int ns = template_all_sets(sets, MAX_NAMES), nd = template_dirs(dirs, MAX_NAMES);

	for (int i = 0; i < ns; i++) {
		int covered = 0;

		char desc[256];

		printf("%c %-24s", strcmp(sets[i], c->set) ? ' ' : '*', sets[i]);
		for (int k = 0; k < nd; k++)
			if (template_has(dirs[k], sets[i])) {
				printf(" %s", dirs[k]);
				covered++;
			}
		if (covered == nd)
			printf("  (complete)");
		printf("\n");
		if (set_info(sets[i], "description", desc, sizeof desc) == 0 && desc[0])
			printf("  %s\n", desc);
	}
	return 0;
}

/* themer set copy FROM TO: see front_set_copy */
static int
cmd_set_copy(const char *from, const char *to)
{
	int n = front_set_copy(from, to, stdout);

	if (n == -2) {
		fprintf(stderr, "themer: no set '%s', or '%s' is not a name for a new set\n", from, to);
		return 1;
	}
	if (n < 0) {
		fprintf(stderr, "themer: could not copy %s\n", from);
		return 1;
	}
	printf("edit ~/.config/themer/templates/%s/, then: themer set %s\n", to, to);
	return 0;
}

/* themer template list DIR [SET]: DIR is a template directory, or the name of a
 * module, whose directory is then listed */
static int
cmd_template_list(const char *dir, const char *set)
{
	char names[MAX_NAMES][NAME_LEN];
	int i = themer_module_find(dir), n;

	if (i >= 0 && themer_module_templates(i))
		dir = themer_module_templates(i);
	n = set ? template_files(dir, set, names, MAX_NAMES) : template_sets(dir, names, MAX_NAMES);
	for (i = 0; i < n; i++)
		puts(names[i]);
	return 0;
}

/* themer template render MODULE SET FILE [OUT]: renders one file of a template set
 * with the current theme and mode, to stdout or OUT, for copying elsewhere. */
static int
cmd_template_render(const Config *c, const char *module, const char *set, const char *file,
                    const char *outpath)
{
	static char out[2 * 1024 * 1024];
	Scheme s;
	TmTheme t;
	TmVar v[TM_MAX_VARS];
	int i = themer_module_find(module), nv;
	FILE *f = stdout;

	if (i < 0)
		return no_module(module);
	if (!themer_module_templates(i)) {
		fprintf(stderr, "themer: %s renders no templates\n", module);
		return 1;
	}
	themer_module_view(c, i, &s, &t);
	nv = themer_module_vars(i, &t, v, (int)TM_COUNT(v));
	if (nv < 0 || tm_render(out, sizeof out, themer_module_templates(i), set, file, &t, v, nv) < 0)
		return 1;
	if (outpath && !(f = fopen(outpath, "w"))) {
		perror(outpath);
		return 1;
	}
	fputs(out, f);
	if (f != stdout)
		fclose(f);
	return 0;
}

int
main(int argc, char *argv[])
{
	const char *cmd = argc > 1 ? argv[1] : "tui";
	const char *sub = argc > 2 ? argv[2] : "";
	Config c;
	int r = 0;

	if (!strcmp(cmd, "-v") || !strcmp(cmd, "version")) {
		puts("themer " VERSION);
		return 0;
	}
	if (!strcmp(cmd, "system"))
		return system_cmd(argc, argv);   /* root's side: no state of its own */
	if (themer_startup(&c) < 0)
		return 1;

	/* commands that change nothing */
	if (!strcmp(cmd, "tui")) {
		if (!isatty(STDIN_FILENO)) {
			usage();
			return 1;
		}
		tui_run(&c);
		return 0;
	} else if (!strcmp(cmd, "list")) {
		static char names[MAX_THEMES][NAME_LEN];
		int n = theme_list(names, MAX_THEMES);

		for (int i = 0; i < n; i++)
			puts(names[i]);
		return 0;
	} else if (!strcmp(cmd, "check")) {
		return cmd_check();
	} else if (!strcmp(cmd, "template") && !strcmp(sub, "list") && argc > 3) {
		return cmd_template_list(argv[3], argc > 4 ? argv[4] : NULL);
	} else if (!strcmp(cmd, "template") && !strcmp(sub, "render") && argc > 5) {
		return cmd_template_render(&c, argv[3], argv[4], argv[5], argc > 6 ? argv[6] : NULL);
	} else if (!strcmp(cmd, "module") && !strcmp(sub, "list")) {
		int order[MAX_MODULES], n = front_module_order(&c, order, MAX_MODULES), last = -1;

		/* in the groups every front end shows: loaded, unloaded, not installed */
		for (int k = 0; k < n; k++) {
			int i = order[k];

			if (front_module_group(&c, i) != last) {
				last = front_module_group(&c, i);
				printf("%s# %s\n", k ? "\n" : "", front_module_group_title(last));
			}
			printf("%-12s %-3s %-8s %s\n", themer_module_name(i),
			       themer_module_on(&c, i) ? "on" : "off",
			       themer_module_present(&c, i) ? "" : "absent", themer_module_desc(i));
		}
		return 0;
	} else if (!strcmp(cmd, "module") && !strcmp(sub, "options") && argc > 3) {
		int i = themer_module_find(argv[3]);

		return i < 0 ? no_module(argv[3]) : cmd_module_options(&c, i);
	} else if (!strcmp(cmd, "set") && argc == 2) {
		puts(c.set);
		return 0;
	} else if ((!strcmp(cmd, "icons") || !strcmp(cmd, "cursor")) && argc == 2) {
		/* the current one, then every installed one */
		char names[MAX_NAMES][NAME_LEN];
		int cur = cmd[0] == 'c', n = icon_themes_scan(names, MAX_NAMES, cur);

		if (cur)
			printf("* %s %d\n", c.cursor, c.cursor_size);
		else
			printf("* %s\n", c.icons);
		for (int i = 0; i < n; i++)
			if (strcmp(names[i], cur ? c.cursor : c.icons))
				printf("  %s\n", names[i]);
		return 0;
	} else if (!strcmp(cmd, "render") && argc == 2) {
		printf("hinting %s\nantialias %s\nrgba %s\n", c.hinting, c.antialias ? "on" : "off", c.rgba);
		return 0;
	} else if (!strcmp(cmd, "set") && !strcmp(sub, "list")) {
		return cmd_set_list(&c);
	} else if (!strcmp(cmd, "set") && !strcmp(sub, "copy") && argc > 4) {
		return cmd_set_copy(argv[3], argv[4]);
	} else if (!strcmp(cmd, "set") && !strcmp(sub, "import") && argc > 5) {
		return set_import(&c, argv[3], argv[4], argv[5], stdout) < 0 ? 1 : 0;
	} else if (!strcmp(cmd, "set") && !strcmp(sub, "check") && argc > 3) {
		return set_check(&c, argv[3], stdout) < 0 ? 1 : 0;
	}

	/* commands that change the state, saved below */
	if (!strcmp(cmd, "apply")) {
		r = themer_apply(&c);
	} else if (!strcmp(cmd, "remove") && argc == 2) {
		char msg[256];

		r = front_remove(&c, msg, sizeof msg);
		puts(msg);
		return r ? 1 : 0;               /* saved by front_remove */
	} else if (!strcmp(cmd, "restore") && argc == 2) {
		c.removed = 0;
		r = themer_apply(&c);
	} else if (!strcmp(cmd, "toggle")) {
		c.mode = c.mode == MODE_DARK ? MODE_LIGHT : MODE_DARK;
		r = themer_apply(&c);
	} else if (!strcmp(cmd, "dark") || !strcmp(cmd, "light")) {
		c.mode = cmd[0] == 'd' ? MODE_DARK : MODE_LIGHT;
		r = themer_apply(&c);
	} else if (!strcmp(cmd, "theme") && argc > 2) {
		if (front_set_theme(&c, argv[2]) < 0) {
			fprintf(stderr, "themer: no theme '%s'\n", argv[2]);
			return 1;
		}
		r = themer_apply(&c);
	} else if (!strcmp(cmd, "set") && argc > 2) {
		if (front_set_set(&c, argv[2]) < 0) {
			fprintf(stderr, "themer: no template set '%s' (themer set list)\n", argv[2]);
			return 1;
		}
		r = themer_apply(&c);
	} else if (!strcmp(cmd, "icons") && argc > 2) {
		if (front_set_icons(&c, argv[2]) < 0)
			return 1;
		r = themer_apply(&c);
	} else if (!strcmp(cmd, "cursor") && argc > 2) {
		if ((argc > 3 && atoi(argv[3]) <= 0) ||
		    front_set_cursor(&c, argv[2], argc > 3 ? atoi(argv[3]) : 0) < 0) {
			fprintf(stderr, "themer: the cursor size is a number of pixels\n");
			return 1;
		}
		r = themer_apply(&c);
	} else if (!strcmp(cmd, "font") && argc > 3 && (!strcmp(sub, "ui") || !strcmp(sub, "mono"))) {
		if ((argc > 4 && atoi(argv[4]) <= 0) ||
		    front_set_font(&c, sub[0] == 'm', argv[3], argc > 4 ? atoi(argv[4]) : 0) < 0) {
			fprintf(stderr, "themer: the font size is a number of points\n");
			return 1;
		}
		r = themer_apply(&c);
	} else if (!strcmp(cmd, "font") && argc > 3 && !strcmp(sub, "tty")) {
		if (front_set_tty_font(&c, argv[3]) < 0) {
			fprintf(stderr, "themer: a console font name is letters, digits, - and _ (as in ter-v16n)\n");
			return 1;
		}
		r = themer_apply(&c);
	} else if (!strcmp(cmd, "font") && argc == 3 && !strcmp(sub, "tty")) {
		static char names[1024][NAME_LEN];
		int n = front_tty_fonts(names, 1024);

		for (int i = 0; i < n; i++)
			printf("%s%s\n", names[i], strcmp(names[i], c.tty_font) ? "" : "  *");
		return 0;
	} else if (!strcmp(cmd, "color") && argc == 2) {
		Scheme s;

		theme_scheme(&c, &s);
		for (int i = 0; i < C_N; i++)
			printf("%-6s %s%s\n", color_names[i], s.hex[i], c.color[i][0] ? "  (yours)" : "");
		return 0;
	} else if (!strcmp(cmd, "color") && argc == 4) {
		int role = front_color_role(argv[2]);

		if (role < 0) {
			fprintf(stderr, "themer: no color role %s (bg fg surf acc accfg urg warn)\n", argv[2]);
			return 1;
		}
		if (front_set_color(&c, role, strcmp(argv[3], "reset") ? argv[3] : NULL) < 0) {
			fprintf(stderr, "themer: a color is six hex digits, as 5f87af or #5f87af, or reset\n");
			return 1;
		}
		r = themer_apply(&c);
	} else if (!strcmp(cmd, "render") && argc > 3 && argc % 2 == 0) {
		for (int i = 2; i + 1 < argc; i += 2) {
			int ok;

			if (!strcmp(argv[i], "hinting"))
				ok = front_set_hinting(&c, argv[i + 1]) == 0;
			else if (!strcmp(argv[i], "rgba"))
				ok = front_set_rgba(&c, argv[i + 1]) == 0;
			else if (!strcmp(argv[i], "antialias") && (!strcmp(argv[i + 1], "on") || !strcmp(argv[i + 1], "off"))) {
				front_set_antialias(&c, argv[i + 1][1] == 'n');
				ok = 1;
			} else
				ok = 0;
			if (!ok) {
				fprintf(stderr, "themer: '%s %s' is not a rendering setting\n", argv[i], argv[i + 1]);
				return 1;
			}
		}
		r = themer_apply(&c);
	} else if (!strcmp(cmd, "env") && argc == 2) {
		for (int i = 0; i < c.nenv; i++)
			printf("%s=%s\n", c.env[i].name, c.env[i].value);
		return 0;
	} else if (!strcmp(cmd, "env") && argc > 2 && strchr(argv[2], '=')) {
		char *eq = strchr(argv[2], '='), msg[160];

		*eq = '\0';
		if (front_env_set(&c, argv[2], eq + 1, msg, sizeof msg) < 0) {
			fprintf(stderr, "themer: %s\n", msg);
			return 1;
		}
		r = themer_apply(&c);
	} else if (!strcmp(cmd, "module") && (!strcmp(sub, "load") || !strcmp(sub, "unload")) && argc > 3) {
		int i = themer_module_find(argv[3]);

		if (i < 0)
			return no_module(argv[3]);
		r = themer_module_set(&c, i, sub[0] == 'l');
		if (r == -3) {
			fprintf(stderr, "themer: %s: %s\n", argv[3], front_text(FT_MODULE_ABSENT));
			return 1;
		}
	} else if (!strcmp(cmd, "module") && ((!strcmp(sub, "option") && argc > 5) ||
	                                      (!strcmp(sub, "reset") && argc > 4))) {
		int i = themer_module_find(argv[3]), j = i < 0 ? -1 : themer_option_find(i, argv[4]);
		char msg[256];

		if (i < 0 || j < 0) {
			fprintf(stderr, "themer: no such module option\n");
			return 1;
		}
		if ((sub[0] == 'o' ? front_option_set(&c, i, j, argv[5], msg, sizeof msg)
		                   : front_option_reset(&c, i, j, msg, sizeof msg)) < 0) {
			fprintf(stderr, "themer: %s\n", msg);
			return 1;
		}
		return 0;           /* front_option_* saved the state */
	} else if (!strcmp(cmd, "scheme") && !strcmp(sub, "import") && argc > 3) {
		return scheme_import(argv[3], argc > 4 ? argv[4] : NULL, stdout) > 0 ? 0 : 1;
	} else if (!strcmp(cmd, "scheme") && !strcmp(sub, "remove") && argc > 3) {
		char msg[256];
		int bad = front_scheme_remove(&c, argv[3], msg, sizeof msg) < 0;

		fprintf(bad ? stderr : stdout, "%s%s\n", bad ? "themer: " : "", msg);
		return bad;
	} else if (!strcmp(cmd, "scheme") && !strcmp(sub, "restore")) {
		if (scheme_restore(argc > 3 ? argv[3] : NULL) < 0) {
			fprintf(stderr, "themer: could not restore\n");
			return 1;
		}
		return 0;
	} else if (!strcmp(cmd, "server") && !strcmp(sub, "install")) {
		char msg[256];

		r = front_stylus_install(&c, argc > 3 ? argv[3] : NULL, msg, sizeof msg);
		fprintf(r ? stderr : stdout, "themer: %s\n", msg);
		return r ? 1 : 0;
	} else if (!strcmp(cmd, "server") && !strcmp(sub, "port") && argc > 3) {
		if (front_set_web_port(&c, atoi(argv[3])) < 0) {
			fprintf(stderr, "themer: the port is a number from 1024 to 65535\n");
			return 1;
		}
		r = 0;
	} else if (!strcmp(cmd, "server") && (!strcmp(sub, "start") || !strcmp(sub, "stop") ||
	                                      !strcmp(sub, "status"))) {
		int port = front_web_port(&c), up = front_web_running(port);

		if (!strcmp(sub, "status")) {
			if (up)
				printf("themer-web: running, http://localhost:%d/\n", port);
			else
				printf("themer-web: stopped (port %d)\n", port);
			return up ? 0 : 3;
		}
		if ((sub[2] == 'a') == up)       /* already started, or already stopped */
			return 0;
		if ((sub[2] == 'a' ? front_web_start(port) : front_web_stop(port)) < 0) {
			fprintf(stderr, "themer: could not %s themer-web on port %d%s\n", sub, port,
			        sub[2] == 'a' ? " (is a browser module on, and themer-web installed?)" : "");
			return 1;
		}
		return 0;
	} else {
		usage();
		return 1;
	}
	if (state_save(&c) < 0)
		fprintf(stderr, "themer: could not save state\n");
	return r ? 1 : 0;
}
