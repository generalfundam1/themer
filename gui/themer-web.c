/* themer-web.c: the web front end. A small HTTP server on localhost that serves
 * one page, the same settings as the TUI, themer-gtk and themer-qt (the theme, the
 * mode, the template set, fonts, icons, the cursor, rendering, the modules and
 * their options, Apply), and the stylus module's userstyles, so the Stylus
 * extension installs them from http://localhost:PORT/stylus/<site>.user.css and
 * keeps them current through @updateURL without a tab kept open.
 *
 * It is for the browser, so it runs only while a browser module (firefox or
 * stylus) is on: without one it says so and exits, and a running one exits within
 * a few seconds of the last browser module being turned off. It is started and
 * stopped by hand: System, Server in any front end (themer-web -d, detached; themer-web -q
 * to stop), themer server start|stop, or a user service.
 *
 * One thread, one request at a time, localhost only: the page is a local control
 * panel, not a service. Other web pages open in the same browser can reach
 * localhost too, so the server answers only requests whose Host is
 * localhost:PORT or 127.0.0.1:PORT (DNS rebinding), sends no CORS header (no other
 * origin may read an answer), and takes a POST only with an "X-Themer: 1" header,
 * which a page on another origin cannot send without a preflight the server never
 * grants. The page itself is gui/web/index.html, built into the
 * binary (gui/web_page.h) and styled from the theme being browsed through
 * /theme.css, so the page is the preview, as in the other front ends.
 *
 * Routes:
 *   GET  /                      the page
 *   GET  /theme.css             the browsed theme as CSS variables
 *   GET  /page.css              the page's look: the web module's template, web/page.css
 *   GET  /api/state             everything the page shows, as JSON
 *   GET  /api/text              the pages and every word on them (text.c), the size limits
 *   POST /api/set               {theme|mode|set|ui_font|ui_size|mono_font|mono_size|
 *                                icons|cursor|cursor_size|hinting|antialias|rgba}
 *                               changes the browsed settings (not applied yet)
 *   POST /api/apply             applies and saves, as themer apply
 *   POST /api/revert            back to the applied state
 *   POST /api/module            {name, on}: load or unload a module now
 *   POST /api/option            {module, option, value}: set an option now; a new
 *                               stylus port moves the server there ({"moved": PORT})
 *   POST /api/option/reset      {module, option}: back to the option's default
 *   POST /api/set/copy          {from, to}; /api/set/check {name};
 *   POST /api/set/import        {set, module, path}: the set tools, report as text
 *   POST /api/scheme/import     {name, text}: a scheme file's text, imported (report)
 *   POST /api/scheme/remove     {name}; POST /api/scheme/restore: every hidden one
 *   POST /api/quit              stops the server
 *   GET  /stylus/<site>.user.css   the rendered userstyle
 *   GET  /icon/<name>           an icon of the icon theme being browsed (48px), as GTK
 *                               and Qt would draw it, for the Icons page
 *
 * Usage: themer-web [-p PORT] [-d | -q]
 *   -d  detach; -q  ask the themer-web on PORT to quit, and wait for it
 *   Started by hand while one already serves PORT, it opens that one's page in the
 *   browser (xdg-open) instead.
 *   PORT defaults to the saved one (Server in the front ends), 7331 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include "themer.h"
#include "common.h"
#include "web_page.h"

#define REQ_CAP (64 * 1024)

static Config cfg, applied;
static int port, port_set;       /* port_set: this request set a new port */

/* What the server does once the current request is answered: a request never stops
 * or moves the server from inside its own handler (it would wait on itself). */
enum { AFTER_NONE, AFTER_QUIT, AFTER_MOVE };
static int after, move_to;

/* a new port in the state (set through the page) moves the server once answered */
static int
follow_port(void)
{
	int newport = front_web_port(&cfg);

	if (newport == port)
		return 0;
	after = AFTER_MOVE;
	move_to = newport;
	return newport;
}

/* ---- a growing string ---- */
typedef struct { char *s; size_t n, cap; } Buf;

static void
bput(Buf *b, const char *s, size_t len)
{
	if (b->n + len + 1 > b->cap) {
		size_t cap = b->cap ? b->cap * 2 : 8192;

		while (cap < b->n + len + 1)
			cap *= 2;
		b->s = realloc(b->s, cap);
		if (!b->s) {
			perror("themer-web");
			exit(1);
		}
		b->cap = cap;
	}
	memcpy(b->s + b->n, s, len);
	b->n += len;
	b->s[b->n] = '\0';
}

static void
bputs(Buf *b, const char *s)
{
	bput(b, s, strlen(s));
}

static void
bprintf(Buf *b, const char *fmt, ...)
{
	char tmp[4096];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(tmp, sizeof tmp, fmt, ap);
	va_end(ap);
	if (n > 0)
		bput(b, tmp, (size_t)n < sizeof tmp ? (size_t)n : sizeof tmp - 1);
}

/* a JSON string, quoted and escaped */
static void
jstr(Buf *b, const char *s)
{
	bput(b, "\"", 1);
	for (; s && *s; s++) {
		unsigned char c = (unsigned char)*s;

		if (c == '"' || c == '\\')
			bprintf(b, "\\%c", c);
		else if (c < 0x20)
			bprintf(b, "\\u%04x", c);
		else
			bput(b, s, 1);
	}
	bput(b, "\"", 1);
}

/* ---- a flat JSON object in: "key": "string" | number | true | false ----
 * The key is matched only where a key can be (after { or ,), and a string value has
 * its escapes decoded (\uXXXX as UTF-8). Returns 1 when the key is there. */
static int
jget(const char *body, const char *key, char *out, size_t n)
{
	char pat[80];
	const char *p = body, *q;
	size_t o = 0;

	snprintf(pat, sizeof pat, "\"%s\"", key);
	for (; (p = strstr(p, pat)); p++) {
		const char *b = p;

		while (b > body && strchr(" \t\r\n", b[-1]))
			b--;
		if (b == body || (b[-1] != '{' && b[-1] != ','))
			continue;                    /* inside a value, not a key */
		q = p + strlen(pat);
		while (*q && strchr(" \t\r\n", *q))
			q++;
		if (*q != ':')
			continue;
		q++;
		while (*q && strchr(" \t\r\n", *q))
			q++;
		if (*q == '"') {
			for (q++; *q && *q != '"' && o + 4 < n; q++) {
				unsigned u;

				if (*q != '\\') {
					out[o++] = *q;
					continue;
				}
				switch (*++q) {
				case 'n': out[o++] = '\n'; break;
				case 't': out[o++] = '\t'; break;
				case 'r': out[o++] = '\r'; break;
				case 'b': out[o++] = '\b'; break;
				case 'f': out[o++] = '\f'; break;
				case 'u':
					if (sscanf(q + 1, "%4x", &u) != 1 || strspn(q + 1, "0123456789abcdefABCDEF") < 4)
						goto bad;
					q += 4;
					if (u < 0x80) {
						out[o++] = (char)u;
					} else if (u < 0x800) {
						out[o++] = (char)(0xc0 | u >> 6);
						out[o++] = (char)(0x80 | (u & 0x3f));
					} else {
						out[o++] = (char)(0xe0 | u >> 12);
						out[o++] = (char)(0x80 | ((u >> 6) & 0x3f));
						out[o++] = (char)(0x80 | (u & 0x3f));
					}
					break;
				case '\0':
					goto bad;
				default:  out[o++] = *q;      /* \" \\ \/ */
				}
			}
		} else {
			while (*q && !strchr(",} \t\r\n", *q) && o + 1 < n)
				out[o++] = *q++;
		}
		out[o] = '\0';
		return 1;
	}
bad:
	out[0] = '\0';
	return 0;
}

/* ---- the state as JSON ---- */
static void
jlist(Buf *b, const char *name, char names[][NAME_LEN], int n)
{
	bprintf(b, "\"%s\":[", name);
	for (int i = 0; i < n; i++) {
		if (i)
			bput(b, ",", 1);
		jstr(b, names[i]);
	}
	bput(b, "]", 1);
}

static void
state_json(Buf *b)
{
	static char names[MAX_THEMES][NAME_LEN];
	Scheme s;
	int n, order[MAX_MODULES], nm;

	gui_scheme(&cfg, &s);
	bput(b, "{", 1);
	bputs(b, "\"theme\":"); jstr(b, cfg.th.name);
	bprintf(b, ",\"mode\":\"%s\"", cfg.mode == MODE_DARK ? "dark" : "light");
	bputs(b, ",\"set\":"); jstr(b, cfg.set);
	bputs(b, ",\"ui_font\":"); jstr(b, cfg.ui_font);
	bprintf(b, ",\"ui_size\":%d", cfg.ui_size);
	bputs(b, ",\"mono_font\":"); jstr(b, cfg.mono_font);
	bprintf(b, ",\"mono_size\":%d", cfg.mono_size);
	bputs(b, ",\"icons\":"); jstr(b, cfg.icons);
	bputs(b, ",\"cursor\":"); jstr(b, cfg.cursor);
	bprintf(b, ",\"cursor_size\":%d", cfg.cursor_size);
	bputs(b, ",\"hinting\":"); jstr(b, cfg.hinting);
	bprintf(b, ",\"antialias\":%s", cfg.antialias ? "true" : "false");
	bputs(b, ",\"rgba\":"); jstr(b, cfg.rgba);
	bputs(b, ",\"tty_font\":"); jstr(b, cfg.tty_font);
	/* the user's single colors over the scheme ("" where the scheme's applies) and
	 * the session environment */
	bputs(b, ",\"overrides\":{");
	for (int i = 0; i < C_N; i++)
		bprintf(b, "%s\"%s\":\"%s\"", i ? "," : "", color_names[i], cfg.color[i]);
	bputs(b, "},\"env\":[");
	for (int i = 0; i < cfg.nenv; i++) {
		bputs(b, i ? ",[" : "[");
		jstr(b, cfg.env[i].name);
		bput(b, ",", 1);
		jstr(b, cfg.env[i].value);
		bput(b, "]", 1);
	}
	bput(b, "]", 1);
	bprintf(b, ",\"dirty\":%s", config_same(&cfg, &applied) ? "false" : "true");
	bprintf(b, ",\"port\":%d,\"web_port\":%d", port, front_web_port(&cfg));
	bputs(b, ",\"colors\":{");
	for (int i = 0; i < C_N; i++)
		bprintf(b, "%s\"%s\":\"%s\"", i ? "," : "", color_names[i], s.hex[i]);
	bprintf(b, ",\"line\":\"%s\",\"on_urg\":\"%s\",\"on_warn\":\"%s\",\"ansi\":[", s.line, s.on_urg, s.on_warn);
	for (int i = 0; i < 16; i++)
		bprintf(b, "%s\"%s\"", i ? "," : "", s.ansi[i]);
	bputs(b, "]}");
	/* every theme with its swatch in the current mode */
	n = theme_list(names, MAX_THEMES);
	bputs(b, ",\"themes\":[");
	for (int i = 0, emitted = 0; i < n; i++) {
		static Config t;
		Scheme ts;

		t = cfg;
		if (theme_load(names[i], &t.th) < 0)
			continue;              /* a broken theme file is left out */
		gui_scheme(&t, &ts);
		bprintf(b, "%s{\"name\":", emitted++ ? "," : "");
		jstr(b, names[i]);
		bprintf(b, ",\"bg\":\"%s\",\"fg\":\"%s\",\"acc\":\"%s\"}", ts.hex[C_BG], ts.hex[C_FG], ts.hex[C_ACC]);
	}
	bput(b, "]", 1);
	/* the sets as id (directory name) and title (set.conf name=) */
	n = template_all_sets(names, 512);
	bputs(b, ",\"sets\":[");
	for (int i = 0; i < n; i++) {
		char title[NAME_LEN];

		bputs(b, i ? ",{\"id\":" : "{\"id\":");
		jstr(b, names[i]);
		bputs(b, ",\"title\":");
		jstr(b, front_set_title(names[i], title, sizeof title));
		bput(b, "}", 1);
	}
	bput(b, "]", 1);
	n = icon_themes_scan(names, 512, 0);
	bput(b, ",", 1); jlist(b, "icon_themes", names, n);
	n = icon_themes_scan(names, 512, 1);
	bput(b, ",", 1); jlist(b, "cursor_themes", names, n);
	n = fonts_scan(names, 512);
	bput(b, ",", 1); jlist(b, "fonts", names, n);
	n = fonts_mono(names, 512);
	bput(b, ",", 1); jlist(b, "mono_fonts", names, n);
	n = front_tty_fonts(names, 512);
	bput(b, ",", 1); jlist(b, "tty_fonts", names, n);
	bputs(b, ",\"hinting_values\":[");
	for (int i = 0; i < 4; i++) {
		if (i)
			bput(b, ",", 1);
		jstr(b, hinting_values[i]);
	}
	bputs(b, "],\"rgba_values\":[");
	for (int i = 0; i < 5; i++) {
		if (i)
			bput(b, ",", 1);
		jstr(b, rgba_values[i]);
	}
	/* the icons the Icons page shows, the same as themer-gtk's and themer-qt's */
	bputs(b, "],\"preview_icons\":[");
	for (int i = 0; gui_preview_icons[i][0]; i++) {
		bputs(b, i ? ",[" : "[");
		jstr(b, gui_preview_icons[i][0]);
		bput(b, ",", 1);
		jstr(b, gui_preview_icons[i][1]);
		bput(b, "]", 1);
	}
	bputs(b, "],\"groups\":[");
	for (int g = 0; g < OG_N; g++) {
		if (g)
			bput(b, ",", 1);
		jstr(b, front_option_groups[g]);
	}
	bputs(b, "],\"modules\":[");
	nm = front_module_order(order, MAX_MODULES);
	for (int k = 0; k < nm; k++) {
		int i = order[k];

		bprintf(b, "%s{\"name\":", k ? "," : "");
		jstr(b, themer_module_name(i));
		bputs(b, ",\"desc\":"); jstr(b, themer_module_desc(i));
		bputs(b, ",\"state\":"); jstr(b, front_module_state(&cfg, i));
		bputs(b, ",\"note\":"); jstr(b, front_module_note(&cfg, i) ? front_module_note(&cfg, i) : "");
		bprintf(b, ",\"on\":%s,\"present\":%s,\"templates\":%s,\"options\":[",
		        themer_module_on(&cfg, i) ? "true" : "false",
		        themer_module_present(&cfg, i) ? "true" : "false",
		        themer_module_templates(i) ? "true" : "false");
		for (int j = 0; j < themer_option_count(i); j++) {
			const TmOption *o = themer_option(i, j);
			const char *v = themer_option_value(&cfg, i, j);

			bprintf(b, "%s{\"name\":", j ? "," : "");
			jstr(b, o->name);
			bputs(b, ",\"desc\":"); jstr(b, o->desc);
			bputs(b, ",\"choices\":"); jstr(b, o->choices ? o->choices : "");
			bputs(b, ",\"default\":"); jstr(b, o->def ? o->def : "");
			static const char *const controls[] = { "switch", "buttons", "list", "slider", "text" };

			bprintf(b, ",\"min\":%d,\"max\":%d,\"group\":%d,\"set\":%s,\"switch\":%s,\"control\":\"%s\",\"value\":",
			        o->min, o->max, front_option_group(i, j),
			        themer_option_is_set(&cfg, i, j) ? "true" : "false",
			        front_option_is_switch(i, j) ? "true" : "false",
			        controls[front_option_control(i, j)]);
			jstr(b, v ? v : "");
			{
				char note[512];

				front_option_note(&cfg, i, j, note, sizeof note);
				bputs(b, ",\"note\":"); jstr(b, note);
			}
			if (front_option_action(&cfg, i, j)) {
				char link[160];

				bputs(b, ",\"action\":"); jstr(b, front_option_action(&cfg, i, j));
				if (front_option_link(&cfg, i, j, link, sizeof link) == 0) {
					bputs(b, ",\"link\":"); jstr(b, link);
				}
			}
			bput(b, "}", 1);
		}
		bputs(b, "]}");
	}
	bputs(b, "]}");
}

/* the pages, the words (front_text, by key) and the limits: the page fills itself from
 * these, so it reads as the other front ends do */
static void
text_json(Buf *b)
{
	bputs(b, "{\"pages\":[");
	for (int p = 0; p < FP_N; p++) {
		bputs(b, p ? ",{\"id\":" : "{\"id\":");
		jstr(b, front_pages[p].id);
		bputs(b, ",\"title\":");
		jstr(b, front_pages[p].title);
		bput(b, "}", 1);
	}
	bputs(b, "],\"text\":{");
	for (int t = 0; t < FT_N; t++) {
		if (t)
			bput(b, ",", 1);
		jstr(b, front_text_key(t));
		bput(b, ":", 1);
		jstr(b, front_text(t));
	}
	bprintf(b, "},\"limits\":{\"font_min\":%d,\"font_max\":%d,\"cursor_min\":%d,\"cursor_max\":%d}}",
	        FRONT_FONT_MIN, FRONT_FONT_MAX, FRONT_CURSOR_MIN, FRONT_CURSOR_MAX);
}

/* the browsed theme as CSS variables: the page restyles itself from them */
static void
theme_css(Buf *b)
{
	Scheme s;
	TmTheme t;
	SharedStore st;
	TmVar v[MAX_SHARED];
	int nv;

	{
		/* from the web module's side of the theme (its mode option), as /page.css */
		int i = themer_module_find("web");

		if (i >= 0) {
			themer_module_view(&cfg, i, &s, &t);
		} else {
			gui_scheme(&cfg, &s);
			themer_view(&cfg, &s, &t);
		}
	}
	nv = shared_vars(&t, &st, v, MAX_SHARED);
	bputs(b, ":root {\n");
	for (int i = 0; i < nv; i++) {
		/* colors are six hex digits; the rest (fonts, sizes, padding) go as they are */
		if (strlen(v[i].value) == 6 && strspn(v[i].value, "0123456789abcdefABCDEF") == 6)
			bprintf(b, "  --%s: #%s;\n", v[i].name, v[i].value);
		else if (strlen(v[i].name) > 5 && !strcmp(v[i].name + strlen(v[i].name) - 5, "_font") &&
		         !strpbrk(v[i].value, "\"\\\n"))
			/* a family name, quoted: names with digits or symbols are not CSS identifiers */
			bprintf(b, "  --%s: \"%s\";\n", v[i].name, v[i].value);
		else if (strlen(v[i].value) < 120 && !strchr(v[i].value, '\n'))
			bprintf(b, "  --%s: %s;\n", v[i].name, v[i].value);
	}
	bprintf(b, "  color-scheme: %s;\n}\n", t.dark ? "dark" : "light");
}

/* The page's look: web/page.css of the set the web module renders from, as browsed
 * (not yet applied), so it changes with the set as the rest of the preview does */
static void
page_css(Buf *b)
{
	static char css[256 * 1024];
	static Config view;

	int i = themer_module_find("web");

	if (i >= 0 && !themer_module_on(&cfg, i))
		return;               /* the web module off: the browser's own controls */
	view = cfg;
	snprintf(view.set, sizeof view.set, "%s", front_template_set(&cfg, "web"));
	if (front_render(&view, "web", "web", "page.css", css, sizeof css) == 0)
		bputs(b, css);
}

/* ---- the settings ---- */
static int
api_set(const char *body, char *msg, size_t n)
{
	char v[256], v2[32];
	int rc = 0;

	msg[0] = '\0';
	if (jget(body, "theme", v, sizeof v) && front_set_theme(&cfg, v) < 0)
		rc = -1, snprintf(msg, n, "no theme '%.200s'", v);
	if (jget(body, "mode", v, sizeof v))
		cfg.mode = !strcmp(v, "light") ? MODE_LIGHT : MODE_DARK;
	if (jget(body, "set", v, sizeof v) && front_set_set(&cfg, v) < 0)
		rc = -1, snprintf(msg, n, "no template set '%.200s'", v);
	if (jget(body, "ui_font", v, sizeof v) || jget(body, "ui_size", v2, sizeof v2)) {
		int size = jget(body, "ui_size", v2, sizeof v2) ? atoi(v2) : 0;

		if (!jget(body, "ui_font", v, sizeof v))
			snprintf(v, sizeof v, "%s", cfg.ui_font);
		front_set_font(&cfg, 0, v, size);
	}
	if (jget(body, "mono_font", v, sizeof v) || jget(body, "mono_size", v2, sizeof v2)) {
		int size = jget(body, "mono_size", v2, sizeof v2) ? atoi(v2) : 0;

		if (!jget(body, "mono_font", v, sizeof v))
			snprintf(v, sizeof v, "%s", cfg.mono_font);
		front_set_font(&cfg, 1, v, size);
	}
	if (jget(body, "icons", v, sizeof v))
		front_set_icons(&cfg, v);
	if (jget(body, "cursor", v, sizeof v) || jget(body, "cursor_size", v2, sizeof v2)) {
		int size = jget(body, "cursor_size", v2, sizeof v2) ? atoi(v2) : 0;

		front_set_cursor(&cfg, jget(body, "cursor", v, sizeof v) ? v : NULL, size);
	}
	if (jget(body, "hinting", v, sizeof v) && front_set_hinting(&cfg, v) < 0)
		rc = -1, snprintf(msg, n, "bad hinting '%.200s'", v);
	if (jget(body, "rgba", v, sizeof v) && front_set_rgba(&cfg, v) < 0)
		rc = -1, snprintf(msg, n, "bad rgba '%.200s'", v);
	if (jget(body, "antialias", v, sizeof v))
		front_set_antialias(&cfg, !strcmp(v, "true") || !strcmp(v, "1"));
	if (jget(body, "tty_font", v, sizeof v) && front_set_tty_font(&cfg, v) < 0)
		rc = -1, snprintf(msg, n, "a console font name is letters, digits, - and _");
	/* one color over the scheme: {"color": ROLE, "value": rrggbb or "" for the scheme's} */
	if (jget(body, "color", v, sizeof v)) {
		char hex[32] = "";

		jget(body, "value", hex, sizeof hex);
		if (front_set_color(&cfg, front_color_role(v), hex) < 0)
			rc = -1, snprintf(msg, n, "a color is six hex digits");
	}
	/* one environment variable: {"env": NAME, "value": VALUE or "" to remove it} */
	if (jget(body, "env", v, sizeof v)) {
		char val[300] = "";

		jget(body, "value", val, sizeof val);
		if (front_env_set(&cfg, v, val, msg, n) < 0)
			rc = -1;
		else
			msg[0] = '\0';
	}
	/* the port is saved at once (front_set_web_port); the move happens after the
	 * answer, since this server is the one moving */
	if (jget(body, "web_port", v, sizeof v)) {
		int p = atoi(v);

		if (p < 1024 || p > 65535)
			rc = -1, snprintf(msg, n, "the port is a number from 1024 to 65535");
		else if (p != cfg.web_port) {
			cfg.web_port = p;
			front_save_now(&cfg);
			port_set = 1;
		}
	}
	return rc;
}

/* runs a report-writing set tool and returns its text */
static char *
with_report(int (*fn)(FILE *, void *), void *arg, int *rc)
{
	char *text = NULL;
	size_t len = 0;
	FILE *f = open_memstream(&text, &len);

	if (!f)
		return NULL;
	*rc = fn(f, arg);
	fclose(f);
	return text;
}

static int do_check(FILE *f, void *arg) { return set_check(&cfg, arg, f); }
static int do_copy(FILE *f, void *arg) { return front_set_copy(((char **)arg)[0], ((char **)arg)[1], f) < 0 ? -1 : 0; }
static int do_import(FILE *f, void *arg) { char **a = arg; return set_import(&cfg, a[0], a[1], a[2], f); }

/* ---- HTTP ---- */
static void
respond(int fd, int status, const char *type, const char *body, size_t len)
{
	char head[512];
	int n = snprintf(head, sizeof head,
	                 "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
	                 "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",
	                 status, status == 200 ? "OK" : status == 404 ? "Not Found" : status == 400 ? "Bad Request" :
	                 status == 403 ? "Forbidden" : status == 413 ? "Payload Too Large" : status == 500 ? "Internal Server Error" : "Error",
	                 type, len);

	if (write(fd, head, (size_t)n) < 0)
		return;
	for (size_t o = 0; o < len; ) {
		ssize_t w = write(fd, body + o, len - o);

		if (w <= 0)
			return;
		o += (size_t)w;
	}
}

static void
respond_json(int fd, Buf *b)
{
	respond(fd, 200, "application/json; charset=utf-8", b->s ? b->s : "{}", b->n);
}

static void
respond_text(int fd, int status, const char *s)
{
	respond(fd, status, "text/plain; charset=utf-8", s, strlen(s));
}

/* an icon of the browsed icon theme; the name is an icon name, nothing else */
static void
serve_icon(int fd, const char *name)
{
	size_t len = strlen(name);
	const char *mime;
	char *data;
	Scheme sc;

	if (!len || len > 96 || strspn(name, "abcdefghijklmnopqrstuvwxyz0123456789-_.") != len || name[0] == '.') {
		respond_text(fd, 404, "no such icon in the theme\n");
		return;
	}
	gui_scheme(&cfg, &sc);
	if (!(data = gui_icon_load(cfg.icons, name, 48, sc.hex[C_FG], &len, &mime))) {
		respond_text(fd, 404, "no such icon in the theme\n");
		return;
	}
	respond(fd, 200, mime, data, len);
	free(data);
}

/* a userstyle from ~/.config/themer/stylus: the name must be <site>.user.css */
static void
serve_stylus(int fd, const char *name)
{
	char path[PATH_MAX];
	FILE *f;
	Buf b = { 0 };
	char chunk[4096];
	size_t r;
	size_t len = strlen(name);

	if (len < 10 || len > 100 || strcmp(name + len - 9, ".user.css") ||
	    strspn(name, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") != len ||
	    strstr(name, "..")) {
		respond_text(fd, 404, "no such style\n");
		return;
	}
	if (path_under_home(path, sizeof path, ".config/themer/stylus/") < 0 ||
	    strlen(path) + len >= sizeof path) {
		respond_text(fd, 404, "no such style\n");
		return;
	}
	strcat(path, name);
	if (!(f = fopen(path, "r"))) {
		respond_text(fd, 404, "no such style: apply with the stylus module on\n");
		return;
	}
	while ((r = fread(chunk, 1, sizeof chunk, f)) > 0)
		bput(&b, chunk, r);
	fclose(f);
	respond(fd, 200, "text/css; charset=utf-8", b.s ? b.s : "", b.n);
	free(b.s);
}

static void
handle(int fd, const char *method, const char *path, const char *body)
{
	Buf b = { 0 };
	char msg[256];

	front_sync();              /* what another front end saved, merged in first */
	if (!strcmp(method, "GET") && !strcmp(path, "/")) {
		respond(fd, 200, "text/html; charset=utf-8", web_page, strlen(web_page));
	} else if (!strcmp(method, "GET") && !strcmp(path, "/theme.css")) {
		theme_css(&b);
		respond(fd, 200, "text/css; charset=utf-8", b.s, b.n);
	} else if (!strcmp(method, "GET") && !strcmp(path, "/page.css")) {
		page_css(&b);
		respond(fd, 200, "text/css; charset=utf-8", b.s, b.n);
	} else if (!strcmp(method, "GET") && !strcmp(path, "/api/state")) {
		state_json(&b);
		respond_json(fd, &b);
	} else if (!strcmp(method, "GET") && !strcmp(path, "/api/text")) {
		text_json(&b);
		respond_json(fd, &b);
	} else if (!strcmp(method, "GET") && !strncmp(path, "/icon/", 6)) {
		serve_icon(fd, path + 6);
	} else if (!strcmp(method, "GET") && !strncmp(path, "/stylus/", 8)) {
		serve_stylus(fd, path + 8);
	} else if (!strcmp(method, "POST") && !strcmp(path, "/api/set")) {
		if (api_set(body, msg, sizeof msg) < 0) {
			respond_text(fd, 400, msg);
		} else {
			if (port_set)
				follow_port();
			port_set = 0;
			state_json(&b);
			if (after == AFTER_MOVE && b.n && b.s[b.n - 1] == '}') {
				b.s[--b.n] = '\0';
				bprintf(&b, ",\"moved\":%d}", move_to);
			}
			respond_json(fd, &b);
		}
	} else if (!strcmp(method, "POST") && !strcmp(path, "/api/apply")) {
		char msg[256];
		int fails = front_apply(&cfg, msg, sizeof msg);

		applied = cfg;
		bprintf(&b, "{\"failures\":%d,\"message\":", fails);
		jstr(&b, msg);
		bprintf(&b, "}");
		respond_json(fd, &b);
	} else if (!strcmp(method, "POST") && !strncmp(path, "/api/scheme/", 12)) {
		static char text[REQ_CAP];
		char name[NAME_LEN], tmp[PATH_MAX], *report = NULL;
		size_t rlen = 0;
		int rc = -1;
		FILE *rf = open_memstream(&report, &rlen);

		jget(body, "name", name, sizeof name);
		if (!rf) {
			respond_text(fd, 500, "out of memory");
			return;
		}
		if (!strcmp(path + 12, "import")) {
			AFile a;
			FILE *f;

			/* the page sends the file's text: it is imported from a file in the cache */
			jget(body, "text", text, sizeof text);
			if (path_under_home(tmp, sizeof tmp, ".config/themer/cache/import") == 0 &&
			    (f = afile_open(&a, tmp, 0))) {
				fputs(text, f);
				if (afile_commit(&a) == 0) {
					rc = scheme_import(tmp, name[0] ? name : "imported", rf) > 0 ? 0 : -1;
					unlink(tmp);
				}
			}
		} else if (!strcmp(path + 12, "remove")) {
			rc = front_scheme_remove(&applied, name, msg, sizeof msg);
			fputs(msg, rf);
			if (rc == 0 && !strcmp(cfg.th.name, name))
				cfg.th = applied.th;    /* it was only being browsed */
		} else if (!strcmp(path + 12, "restore")) {
			rc = scheme_restore(NULL);
			fputs(front_text(rc == 0 ? FT_SCHEME_RESTORED : FT_SCHEME_RESTORE_FAILED), rf);
		}
		fclose(rf);
		bprintf(&b, "{\"ok\":%s,\"report\":", rc < 0 ? "false" : "true");
		jstr(&b, report ? report : "");
		bput(&b, "}", 1);
		free(report);
		respond_json(fd, &b);
	} else if (!strcmp(method, "POST") && !strcmp(path, "/api/quit")) {
		respond_text(fd, 200, "themer-web: stopped\n");
		after = AFTER_QUIT;
	} else if (!strcmp(method, "POST") && !strcmp(path, "/api/revert")) {
		cfg = applied;
		state_json(&b);
		respond_json(fd, &b);
	} else if (!strcmp(method, "POST") && !strcmp(path, "/api/module")) {
		char name[NAME_LEN], on[8];
		int i;

		jget(body, "name", name, sizeof name);
		jget(body, "on", on, sizeof on);
		i = themer_module_find(name);
		if (i < 0) {
			respond_text(fd, 400, "no such module");
		} else {
			int want = !strcmp(on, "true") || !strcmp(on, "1");
			int rc = front_module_set(&cfg, i, want);   /* saved at once, not pending */

			bprintf(&b, "{\"ok\":%s,\"msg\":", rc < 0 ? "false" : "true");
			jstr(&b, front_text(rc < 0 ? (want ? FT_MODULE_LOAD_FAILED : FT_MODULE_UNLOAD_FAILED)
			                           : (want ? FT_MODULE_LOADED : FT_MODULE_UNLOADED)));
			bput(&b, "}", 1);
			respond_json(fd, &b);
		}
	} else if (!strcmp(method, "POST") && (!strcmp(path, "/api/option") || !strcmp(path, "/api/option/reset"))) {
		char mod[NAME_LEN], opt[NAME_LEN], val[NAME_LEN];
		int i, j, reset = !strcmp(path, "/api/option/reset");

		jget(body, "module", mod, sizeof mod);
		jget(body, "option", opt, sizeof opt);
		jget(body, "value", val, sizeof val);
		i = themer_module_find(mod);
		j = i >= 0 ? themer_option_find(i, opt) : -1;
		if (j < 0) {
			respond_text(fd, 400, "no such option");
		} else {
			int rc = reset ? front_option_reset(&cfg, i, j, msg, sizeof msg)
			               : front_option_set(&cfg, i, j, val, msg, sizeof msg);
			int moved = 0;

			bprintf(&b, "{\"ok\":%s,\"msg\":", rc < 0 ? "false" : "true");
			jstr(&b, msg);
			if (moved)
				bprintf(&b, ",\"moved\":%d", moved);
			bput(&b, "}", 1);
			respond_json(fd, &b);
		}
	} else if (!strcmp(method, "POST") && !strncmp(path, "/api/set/", 9)) {
		char a0[NAME_LEN], a1[NAME_LEN], a2[PATH_MAX], *args[3] = { a0, a1, a2 }, *text;
		int rc = 0;

		if (!strcmp(path + 9, "copy")) {
			jget(body, "from", a0, sizeof a0);
			jget(body, "to", a1, sizeof a1);
			text = with_report(do_copy, args, &rc);
		} else if (!strcmp(path + 9, "check")) {
			jget(body, "name", a0, sizeof a0);
			text = with_report(do_check, a0, &rc);
		} else if (!strcmp(path + 9, "import")) {
			jget(body, "set", a0, sizeof a0);
			jget(body, "module", a1, sizeof a1);
			jget(body, "path", a2, sizeof a2);
			text = with_report(do_import, args, &rc);
		} else {
			respond_text(fd, 404, "no such set tool");
			return;
		}
		bprintf(&b, "{\"ok\":%s,\"report\":", rc < 0 ? "false" : "true");
		jstr(&b, text ? text : "");
		bput(&b, "}", 1);
		free(text);
		respond_json(fd, &b);
	} else {
		respond_text(fd, 404, "not found\n");
	}
	free(b.s);
}

/* the value of a request header, trimmed, or NULL; hend is the end of the head */
static const char *
header(const char *req, const char *hend, const char *name, char *out, size_t n)
{
	size_t len = strlen(name);

	for (const char *l = strchr(req, '\n'); l && l < hend; l = strchr(l + 1, '\n')) {
		const char *v = l + 1, *e;
		size_t o = 0;

		if (strncasecmp(v, name, len) || v[len] != ':')
			continue;
		v += len + 1;
		while (*v == ' ' || *v == '\t')
			v++;
		for (e = v; *e && *e != '\r' && *e != '\n'; e++)
			;
		while (e > v && (e[-1] == ' ' || e[-1] == '\t'))
			e--;
		while (v < e && o + 1 < n)
			out[o++] = *v++;
		out[o] = '\0';
		return out;
	}
	return NULL;
}

/* a request this server answers: Host names the loopback at our port, and a POST
 * carries X-Themer: 1 (see the top of the file) */
static int
allowed(const char *req, const char *hend, const char *method)
{
	char host[128], want1[64], want2[64], x[8];

	if (!header(req, hend, "Host", host, sizeof host))
		return 0;
	snprintf(want1, sizeof want1, "localhost:%d", port);
	snprintf(want2, sizeof want2, "127.0.0.1:%d", port);
	if (strcasecmp(host, want1) && strcmp(host, want2))
		return 0;
	if (!strcmp(method, "POST") && (!header(req, hend, "X-Themer", x, sizeof x) || strcmp(x, "1")))
		return 0;
	return 1;
}

/* reads one request: the header, then Content-Length bytes of body */
static void
serve(int fd)
{
	static char req[REQ_CAP];
	size_t n = 0;
	char *hend, *body, method[8], path[1024];
	long clen = 0;

	for (;;) {
		ssize_t r;

		if (n + 1 >= sizeof req)
			return;
		r = read(fd, req + n, sizeof req - 1 - n);
		if (r <= 0)
			return;
		n += (size_t)r;
		req[n] = '\0';
		if ((hend = strstr(req, "\r\n\r\n")))
			break;
	}
	body = hend + 4;
	for (char *l = req; l && l < hend; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL)
		if (!strncasecmp(l, "Content-Length:", 15)) {
			char *end;

			clen = strtol(l + 15, &end, 10);
			if (end == l + 15 || clen < 0 || clen > (long)(sizeof req - 1 - (size_t)(body - req))) {
				respond_text(fd, 413, "themer-web: bad Content-Length\n");
				return;
			}
			break;
		}
	while ((long)(n - (size_t)(body - req)) < clen) {
		ssize_t r;

		if (n + 1 >= sizeof req)
			return;
		r = read(fd, req + n, sizeof req - 1 - n);
		if (r <= 0)
			return;
		n += (size_t)r;
		req[n] = '\0';
	}
	if (sscanf(req, "%7s %1023s", method, path) != 2)
		return;
	{
		char *q = strchr(path, '?');

		if (q)
			*q = '\0';
	}
	if (!allowed(req, hend, method)) {
		respond_text(fd, 403, "themer-web: only the themer page on localhost may do that\n");
		return;
	}
	handle(fd, method, path, body);
}

/* true if something already listens on port p */
static int
port_busy(int p)
{
	int s = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in a = { 0 };
	int r;

	if (s < 0)
		return 0;
	a.sin_family = AF_INET;
	a.sin_port = htons((unsigned short)p);
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	r = connect(s, (struct sockaddr *)&a, sizeof a) == 0;
	close(s);
	return r;
}

static int
browser_module_on(const Config *c)
{
	static const char *const browsers[] = { "firefox", "stylus" };

	for (size_t i = 0; i < sizeof browsers / sizeof browsers[0]; i++) {
		int k = themer_module_find(browsers[i]);

		if (k >= 0 && themer_module_on(c, k))
			return 1;
	}
	return 0;
}

/* Takes in what another front end or the CLI saved (front_sync): false once no
 * browser module is on any more. */
static int
still_wanted(void)
{
	front_sync();
	return browser_module_on(&cfg);
}

/* sends a request to the themer-web on the port; 0 if it answered */
static int
send_request(const char *method, const char *path, char *buf, size_t bufn)
{
	int s = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in a = { 0 };
	char req[256], own[256];
	int n, ok;
	ssize_t r = 0;

	if (!buf) {
		buf = own;
		bufn = sizeof own;
	}

	if (s < 0)
		return -1;
	a.sin_family = AF_INET;
	a.sin_port = htons((unsigned short)port);
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(s, (struct sockaddr *)&a, sizeof a) < 0) {
		close(s);
		return -1;
	}
	n = snprintf(req, sizeof req, "%s %s HTTP/1.1\r\nHost: localhost:%d\r\nX-Themer: 1\r\n"
	             "Content-Length: 0\r\nConnection: close\r\n\r\n", method, path, port);
	ok = write(s, req, (size_t)n) == n && (r = read(s, buf, bufn - 1)) > 0;
	buf[r > 0 ? r : 0] = '\0';
	close(s);
	return ok ? 0 : -1;
}

int
main(int argc, char *argv[])
{
	int detach = 0, quit = 0, s, one = 1;
	struct sockaddr_in a = { 0 };

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-p") && i + 1 < argc)
			port = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-d"))
			detach = 1;
		else if (!strcmp(argv[i], "-q"))
			quit = 1;
		else {
			fputs("usage: themer-web [-p PORT] [-d | -q]\n", stderr);
			return 1;
		}
	}
	if (themer_startup(&cfg) < 0)
		return 1;
	applied = cfg;
	front_track(&cfg, &applied);
	if (!port)
		port = front_web_port(&cfg);
	if (quit) {
		if (!port_busy(port))
			return 0;
		if (send_request("POST", "/api/quit", NULL, 0) < 0) {
			fprintf(stderr, "themer-web: the server on port %d did not answer\n", port);
			return 1;
		}
		for (int i = 0; i < 50 && port_busy(port); i++)
			usleep(100000);
		return port_busy(port) ? 1 : 0;
	}
	if (!browser_module_on(&cfg)) {
		fputs("themer-web: no browser module is on (firefox, stylus): nothing to serve\n", stderr);
		return 0;
	}
	if (port_busy(port)) {
		char head[256], url[64];

		/* already running: started by hand (not -d, which the front ends use), the
		 * page is what was wanted, so it opens in the browser */
		if (send_request("GET", "/api/state", head, sizeof head) == 0 && !strncmp(head, "HTTP/1.1 200", 12) &&
		    strstr(head, "application/json")) {
			snprintf(url, sizeof url, "http://localhost:%d/", port);
			if (detach)
				return 0;
			if (front_open(url) == 0) {
				printf("themer-web: already running; opened %s\n", url);
				return 0;
			}
			fprintf(stderr, "themer-web: already running at %s (no xdg-open to open it)\n", url);
			return 0;
		}
		fprintf(stderr, "themer-web: something else already listens on port %d\n", port);
		return 1;
	}
	signal(SIGPIPE, SIG_IGN);
	/* the stylus module, run from inside this server (an option set through the
	 * page), leaves starting and stopping the server to it */
	setenv("THEMER_WEB_SERVING", "1", 1);
	s = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (s < 0) {
		perror("themer-web: socket");
		return 1;
	}
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
	a.sin_family = AF_INET;
	a.sin_port = htons((unsigned short)port);
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(s, (struct sockaddr *)&a, sizeof a) < 0 || listen(s, 16) < 0) {
		perror("themer-web: bind");
		return 1;
	}
	front_web_serving(port);
	if (detach) {
		pid_t pid = fork();

		if (pid < 0) {
			perror("themer-web: fork");
			return 1;
		}
		if (pid > 0)
			return 0;
		setsid();
		{
			int null = open("/dev/null", O_RDWR);

			if (null >= 0) {
				dup2(null, 0);
				dup2(null, 1);
				dup2(null, 2);
				close(null);
			}
		}
	} else {
		printf("themer-web: http://localhost:%d/  (userstyles under /stylus/)\n", port);
		fflush(stdout);
	}
	for (;;) {
		struct pollfd pfd = { s, POLLIN, 0 };
		int r = poll(&pfd, 1, 3000), fd;

		if (r < 0 && errno != EINTR)
			break;
		if (!still_wanted())
			break;          /* the last browser module was turned off */
		if (r <= 0)
			continue;
		if ((fd = accept(s, NULL, NULL)) < 0)
			continue;
		fcntl(fd, F_SETFD, FD_CLOEXEC);   /* no program started from here inherits it */
		{
			/* one request at a time: a client that sends nothing cannot hold the server */
			struct timeval tv = { 5, 0 };

			setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
			setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
		}
		serve(fd);
		close(fd);
		if (after == AFTER_QUIT)
			break;
		if (after == AFTER_MOVE) {
			/* the listening socket is closed first, then a new server takes the
			 * new port; this one ends */
			char ps[16];
			pid_t pid;

			close(s);
			snprintf(ps, sizeof ps, "%d", move_to);
			if ((pid = fork()) == 0) {
				execlp("themer-web", "themer-web", "-d", "-p", ps, (char *)NULL);
				_exit(127);
			}
			if (pid > 0)
				waitpid(pid, NULL, 0);
			return 0;
		}
	}
	return 0;
}
