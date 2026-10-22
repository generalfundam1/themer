/* preview.c: a font specimen as an HTML page, drawn in the current theme's
 * colors. A browser renders the installed font, so the page shows the real
 * glyphs, which a terminal can't do for a font other than its own. */
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "themer.h"

/* escapes a value for use inside a CSS string */
static void
css_str(char *out, size_t n, const char *s)
{
	size_t k = 0;

	for (; *s && k + 2 < n; s++) {
		if (*s == '\'' || *s == '\\')
			out[k++] = '\\';
		out[k++] = *s;
	}
	out[k] = '\0';
}

/* writes ~/.cache/themer/fonts/<family>-<size>.html and returns its path */
int
font_preview_write(const Config *c, const Scheme *s, const char *family, int size,
                   char *out, size_t n)
{
	char dir[PATH_MAX], file[PATH_MAX], safe[NAME_LEN], fam[NAME_LEN * 2];
	const char *hex_bg = s->hex[C_BG], *hex_fg = s->hex[C_FG], *hex_surf = s->hex[C_SURF];
	const char *acc = s->hex[C_ACC], *accfg = s->hex[C_ACCFG];
	FILE *f;
	size_t k;

	for (k = 0; family[k] && k < sizeof safe - 1; k++)
		safe[k] = isalnum((unsigned char)family[k]) ? family[k] : '_';
	safe[k] = '\0';
	if (path_under_home(dir, sizeof dir, ".cache/themer/fonts") < 0)
		return -1;
	mkdirs(dir);
	if (snprintf(file, sizeof file, "%s/%s-%d.html", dir, safe, size) >= (int)sizeof file)
		return -1;
	css_str(fam, sizeof fam, family);

	if (!(f = fopen(file, "w")))
		return -1;
	fprintf(f,
	"<!doctype html>\n<html><head><meta charset=\"utf-8\">\n"
	"<title>themer font preview: %s</title>\n<style>\n"
	"body { margin: 0; background: #%s; color: #%s; font-family: sans-serif; }\n"
	"header { padding: 12px 20px; background: #%s; color: #%s; border-bottom: 1px solid #%s; }\n"
	"header b { font-size: 14px; }\n"
	"main { padding: 20px; }\n"
	"pre { margin: 0 0 20px 0; padding: 16px; background: #%s; color: #%s;\n"
	"      border: 1px solid #%s; white-space: pre; overflow-x: auto;\n"
	"      font-family: '%s', monospace; font-size: %dpx; line-height: 1.35; }\n"
	".sel { background: #%s; color: #%s; }\n"
	".k { color: #%s; } .s { color: #%s; } .n { color: #%s; } .c { color: #%s; }\n"
	".f { color: #%s; } .w { color: #%s; } .e { color: #%s; }\n"
	".sz { font-family: '%s', monospace; color: #%s; margin: 4px 0; white-space: pre; }\n"
	"h2 { font-size: 13px; color: #%s; margin: 18px 0 6px; font-weight: normal; }\n"
	"</style></head><body>\n"
	"<header><b>%s</b> &nbsp; %d px &nbsp; theme %s, %s</header>\n<main>\n"
	"<h2>terminal</h2>\n"
	"<pre><span class=\"f\">user@host</span>:<span class=\"c\">~/src/themer</span> $ make\n"
	"<span class=\"s\">cc -std=c99 -Wall -Wextra -Os</span> -c main.c\n"
	"<span class=\"e\">main.c:12:3: error:</span> expected ';' before 'return'\n"
	"<span class=\"w\">warning:</span> unused variable 'ok' [-Wunused-variable]\n\n"
	"int <span class=\"f\">main</span>(void) { <span class=\"k\">for</span> (int i = 0; i &lt; <span class=\"n\">16</span>; i++) ... }\n"
	"x = a-&gt;b != c &amp;&amp; d &gt;= e  f == g  // ligatures, if the font has them\n"
	"<span class=\"sel\"> selected text, highlighted in the accent </span>\n"
	"┌─────────┬─────────┐ █▓▒░ ▲►▼◄ ✓ ✗ ↑↓←→\n"
	"│ box     │ drawing │ 0O 1lI |!  {}[]()&lt;&gt;\n"
	"└─────────┴─────────┘ ~`'\" ,.;:/?\\\\ @#$%%^&amp;*_+-=\n"
	"</pre>\n"
	"<h2>sizes</h2>\n"
	"<div class=\"sz\" style=\"font-size:10px\">10 px  The quick brown fox jumps over the lazy dog 0123456789</div>\n"
	"<div class=\"sz\" style=\"font-size:12px\">12 px  The quick brown fox jumps over the lazy dog 0123456789</div>\n"
	"<div class=\"sz\" style=\"font-size:16px\">16 px  The quick brown fox jumps over the lazy dog</div>\n"
	"<div class=\"sz\" style=\"font-size:22px\">22 px  The quick brown fox</div>\n"
	"<h2>styles</h2>\n"
	"<div class=\"sz\" style=\"font-size:14px\">regular  <i>italic</i>  <b>bold</b>  <b><i>bold italic</i></b></div>\n"
	"</main></body></html>\n",
	fam, hex_bg, hex_fg, hex_surf, hex_fg, acc,
	hex_bg, hex_fg, acc,
	fam, size,
	accfg, acc,
	s->ansi[4], s->ansi[2], s->ansi[5], s->ansi[6],
	s->ansi[3], s->ansi[1], s->ansi[1],
	fam, s->ansi[8],
	acc,
	fam, size, c->th.name, c->mode == MODE_DARK ? "dark" : "light");

	if (fclose(f) != 0)
		return -1;
	snprintf(out, n, "%s", file);
	return 0;
}
