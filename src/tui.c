/* Terminal UI. The whole screen is drawn with the active theme, so a
 * theme change shows up immediately in the interface itself. */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include "themer.h"

enum { K_UP = 1000, K_DOWN, K_LEFT, K_RIGHT, K_ENTER, K_BS, K_ESC, K_REDRAW, K_NONE };
enum { ST_NORM, ST_SEL, ST_DIM, ST_INPUT };

#define MAXL     2048
#define MAX_OPTS 16
#define PANEL_W  72

extern char **environ;

static Config *cfg;
static Scheme sc;
static struct termios orig;
static int rows = 24, cols = 80, dirty, last_key;
static int ptop, pleft, pw, prow, pcol;
static char msg[160];
static char lst[MAXL][NAME_LEN + 32];
static int lstn;

/* ---- color and cursor helpers ---- */

static void
rgb_of(const char *h, int *r, int *g, int *b)
{
	unsigned long v = strtoul(h, NULL, 16);

	*r = (v >> 16) & 255;
	*g = (v >> 8) & 255;
	*b = v & 255;
}

static void
setfg(const char *h)
{
	int r, g, b;

	rgb_of(h, &r, &g, &b);
	printf("\x1b[38;2;%d;%d;%dm", r, g, b);
}

static void
setbg(const char *h)
{
	int r, g, b;

	rgb_of(h, &r, &g, &b);
	printf("\x1b[48;2;%d;%d;%dm", r, g, b);
}

static void
at(int r, int c)
{
	printf("\x1b[%d;%dH", r, c);
}

static void
flash(const char *s)
{
	snprintf(msg, sizeof msg, "%s", s);
}

/* ---- terminal setup ---- */

static void
term_off(void)
{
	fputs("\x1b[0m\x1b[?25h\x1b[?1049l", stdout);
	fflush(stdout);
	tcsetattr(STDIN_FILENO, TCSANOW, &orig);
}

static void
onwinch(int sig)
{
	(void)sig;                     /* read() returns EINTR, which redraws */
}

static void
onsig(int sig)
{
	term_off();
	_exit(128 + sig);
}

static void
term_on(void)
{
	struct termios t;
	struct sigaction sa;

	tcgetattr(STDIN_FILENO, &orig);
	t = orig;
	t.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &t);
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = onwinch;
	sigaction(SIGWINCH, &sa, NULL);
	signal(SIGINT, onsig);
	signal(SIGTERM, onsig);
	setvbuf(stdout, NULL, _IOFBF, 1 << 16);
	fputs("\x1b[?1049h\x1b[?25l", stdout);
}

/* ---- keys ---- */

static int
rd(void)
{
	unsigned char ch;
	ssize_t n;

	n = read(STDIN_FILENO, &ch, 1);
	if (n == 1)
		return ch;
	if (n < 0 && errno == EINTR)
		return -2;
	return -1;
}

static int
readkey(void)
{
	struct termios t, raw;
	int c = rd(), a, b;

	msg[0] = '\0';                 /* a message shows for one frame */
	if (c == -2)
		return K_REDRAW;
	if (c < 0)
		return K_ESC;
	if (c == '\n' || c == '\r')
		return K_ENTER;
	if (c == 127 || c == '\b')
		return K_BS;
	if (c != 27)
		return c;

	/* short timeout tells a bare ESC apart from an escape sequence */
	tcgetattr(STDIN_FILENO, &t);
	raw = t;
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;
	tcsetattr(STDIN_FILENO, TCSANOW, &raw);
	a = rd();
	b = (a == '[' || a == 'O') ? rd() : -1;
	tcsetattr(STDIN_FILENO, TCSANOW, &t);

	if (a != '[' && a != 'O')
		return K_ESC;
	switch (b) {
	case 'A': return K_UP;
	case 'B': return K_DOWN;
	case 'C': return K_RIGHT;
	case 'D': return K_LEFT;
	}
	return K_NONE;
}

/* ---- screen layout ---- */

static void
getsize(void)
{
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
		rows = ws.ws_row;
		cols = ws.ws_col;
	}
}

/* one full-width bar; the last column is never written, so the bottom row can't scroll */
static void
bar(int row, const char *bgc, const char *fgc, const char *left, const char *right)
{
	int max = cols - 1, rw = strlen(right), lw = strlen(left), pad;

	if (rw > max)
		rw = max;
	if (lw > max - rw)
		lw = max - rw;
	at(row, 1);
	setbg(bgc);
	setfg(fgc);
	fputs("\x1b[1m", stdout);
	printf("%.*s", lw, left);
	pad = max - lw - rw;
	printf("%*s", pad, "");
	printf("%.*s", rw, right);
	fputs("\x1b[22m", stdout);
}

static void
frame_begin(const char *hint)
{
	char right[160];
	int r;

	getsize();
	theme_scheme(cfg, &sc);
	fputs("\x1b[0m\x1b[H", stdout);
	for (r = 1; r <= rows; r++) {
		at(r, 1);
		setbg(sc.hex[C_BG]);
		printf("%*s", cols - 1, "");
	}
	snprintf(right, sizeof right, "%s  %s%s ", cfg->th.name,
	         cfg->mode == MODE_DARK ? "dark" : "light", dirty ? "  *" : "");
	bar(1, sc.hex[C_ACC], sc.hex[C_ACCFG], " themer", right);
	bar(rows, sc.hex[C_SURF], sc.hex[C_FG], hint, "");
}

static void
frame_end(void)
{
	if (msg[0]) {
		int len = strlen(msg), col = (cols - len) / 2 + 1;

		if (col < 1)
			col = 1;
		at(rows - 1, col);
		setbg(sc.hex[C_BG]);
		setfg(sc.hex[C_WARN]);
		fputs(msg, stdout);
	}
	fflush(stdout);
}

/* ---- panel: a centered card. pbegin/pline/pbottom draw it line by line ---- */

static void
style(int st)
{
	fputs("\x1b[22m", stdout);
	switch (st) {
	case ST_SEL:   setbg(sc.hex[C_ACC]);  setfg(sc.hex[C_ACCFG]); break;
	case ST_DIM:   setbg(sc.hex[C_SURF]); setfg(sc.ansi[8]); break;
	case ST_INPUT: setbg(sc.hex[C_BG]);   setfg(sc.hex[C_FG]); break;
	default:       setbg(sc.hex[C_SURF]); setfg(sc.hex[C_FG]); break;
	}
}

/* opens a panel whose top-left corner is at (top, left) */
static void
pbegin_at(const char *title, int inner, int w, int left, int top)
{
	int k, tl;
	char t[64];

	(void)inner;
	pw = w;
	pleft = left;
	ptop = top;
	snprintf(t, sizeof t, "%s", title);
	tl = strlen(t);
	if (tl > pw - 4) {
		tl = pw - 4;
		t[tl] = '\0';
	}
	prow = ptop;
	at(prow, pleft);
	setbg(sc.hex[C_SURF]);
	setfg(sc.hex[C_ACC]);
	fputs("┌─ ", stdout);
	fputs("\x1b[1m", stdout);
	setfg(sc.hex[C_FG]);
	printf("%s", t);
	fputs("\x1b[22m", stdout);
	setfg(sc.hex[C_ACC]);
	fputs(" ", stdout);
	for (k = 0; k < pw - tl - 3; k++)
		fputs("─", stdout);
	fputs("┐", stdout);
	prow++;
}

/* a centered panel of inner lines */
static void
pbegin(const char *title, int inner, int w)
{
	int h = inner + 2, width = w > cols - 6 ? cols - 6 : w, left, top;

	if (width < 20)
		width = 20;
	left = (cols - width - 2) / 2 + 1;
	top = (rows - h) / 2 + 1;
	if (top < 3)
		top = 3;
	if (left < 1)
		left = 1;
	pbegin_at(title, inner, width, left, top);
}

static void
pstart(int st)
{
	at(prow, pleft);
	setbg(sc.hex[C_SURF]);
	setfg(sc.hex[C_ACC]);
	fputs("│", stdout);
	style(st);
	pcol = 0;
}

static void
pput(const char *s)
{
	int n = strlen(s), room = pw - pcol;

	if (n > room)
		n = room;
	if (n <= 0)
		return;
	printf("%.*s", n, s);
	pcol += n;
}

static void
pend(void)
{
	while (pcol < pw) {
		putchar(' ');
		pcol++;
	}
	setbg(sc.hex[C_SURF]);
	setfg(sc.hex[C_ACC]);
	fputs("│", stdout);
	prow++;
}

static void
pline(int st, const char *s)
{
	pstart(st);
	pput(s);
	pend();
}

static void
pbottom(void)
{
	int k;

	at(prow, pleft);
	setbg(sc.hex[C_SURF]);
	setfg(sc.hex[C_ACC]);
	fputs("└", stdout);
	for (k = 0; k < pw; k++)
		fputs("─", stdout);
	fputs("┘", stdout);
}

/* a 7-color row, a 16-color row and an ANSI row: the theme, shown in itself */
static void
preview_lines(void)
{
	static const int sem[] = { C_BG, C_FG, C_SURF, C_ACC, C_ACCFG, C_URG, C_WARN };
	int i;

	pstart(ST_NORM);
	for (i = 0; i < 8; i++) {
		setfg(sc.ansi[i]);
		fputs("██", stdout);
		pcol += 2;
	}
	pend();
	pstart(ST_NORM);
	for (i = 8; i < 16; i++) {
		setfg(sc.ansi[i]);
		fputs("██", stdout);
		pcol += 2;
	}
	pend();
	pstart(ST_NORM);
	for (i = 0; i < 7; i++) {
		setfg(sc.hex[sem[i]]);
		fputs("██", stdout);
		pcol += 2;
	}
	pend();
}

/* ---- lists: fonts, icon themes, console fonts ---- */

static int
cmp_str(const void *a, const void *b)
{
	return strcmp(a, b);
}

static void
finish_list(void)
{
	int i, j = 0;

	qsort(lst, lstn, sizeof lst[0], cmp_str);
	for (i = 0; i < lstn; i++)
		if (!j || strcmp(lst[i], lst[j - 1])) {
			if (i != j)
				memcpy(lst[j], lst[i], sizeof lst[0]);
			j++;
		}
	lstn = j;
}

/* names of entries in dir; strip cuts at the first dot */
static void
load_dir(const char *dir, int strip, int want_dirs)
{
	DIR *d = opendir(dir);
	struct dirent *e;

	if (!d)
		return;
	while ((e = readdir(d)) && lstn < MAXL) {
		char name[sizeof lst[0]];

		if (e->d_name[0] == '.')
			continue;
		if (want_dirs && e->d_type != DT_DIR && e->d_type != DT_LNK)
			continue;
		if (!strcmp(e->d_name, "hicolor") || !strcmp(e->d_name, "locolor") ||
		    !strcmp(e->d_name, "default"))
			continue;
		snprintf(name, sizeof name, "%.*s", (int)sizeof name - 1, e->d_name);
		if (strip) {
			char *dot = strchr(name, '.');

			if (dot)
				*dot = '\0';
		}
		if (name[0])
			snprintf(lst[lstn++], sizeof lst[0], "%s", name);
	}
	closedir(d);
}

/* installed font families: fc-list plus the font files themselves (fonts.c) */
static int
load_fonts(int mono)
{
	static char names[8192][NAME_LEN];
	int n, i;

	n = mono ? fonts_mono(names, 8192) : 0;
	if (!n)
		n = fonts_scan(names, 8192);
	lstn = 0;
	for (i = 0; i < n && lstn < MAXL; i++)
		snprintf(lst[lstn++], sizeof lst[0], "%s", names[i]);
	return lstn > 0;
}

/* ---- widgets ---- */

/* returns the selected index on enter, -1 on back, -2 if another key was pressed */
static int
pick(const char *title, const char *sub, const char **items, int n, int *sel, const char *hint)
{
	for (;;) {
		int vis, off, i, k;
		char b[192];

		getsize();
		vis = rows - 12;
		if (vis > n)
			vis = n;
		if (vis < 1)
			vis = 1;
		if (*sel >= n)
			*sel = n - 1;
		if (*sel < 0)
			*sel = 0;
		off = *sel - vis / 2;
		if (off > n - vis)
			off = n - vis;
		if (off < 0)
			off = 0;

		frame_begin(hint);
		pbegin(title, vis + 3, PANEL_W);
		pline(ST_DIM, sub);
		pline(ST_NORM, "");
		for (i = 0; i < vis; i++) {
			snprintf(b, sizeof b, "  %s", items[off + i]);
			pline(off + i == *sel ? ST_SEL : ST_NORM, b);
		}
		pline(ST_NORM, "");
		pbottom();
		frame_end();

		k = readkey();
		switch (k) {
		case 'j':
		case K_DOWN:
			if (*sel < n - 1)
				(*sel)++;
			break;
		case 'k':
		case K_UP:
			if (*sel > 0)
				(*sel)--;
			break;
		case K_ENTER:
			return *sel;
		case 'q':
		case K_ESC:
			return -1;
		case K_REDRAW:
			break;
		default:
			last_key = k;
			return -2;
		}
	}
}

/* filterable list over lst[]: type to narrow, enter chooses. returns 1 on choice */
/* writes a specimen for family and opens it in the browser when xdg-open exists */
static void
show_preview(const char *family)
{
	char path[PATH_MAX], text[PATH_MAX + 64];
	Scheme sch;

	theme_scheme(cfg, &sch);
	if (font_preview_write(cfg, &sch, family, cfg->mono_size, path, sizeof path) < 0) {
		flash("could not write the font preview");
		return;
	}
	if (tm_have("xdg-open")) {
		snprintf(text, sizeof text, "xdg-open '%s' >/dev/null 2>&1 &", path);
		if (system(text) != 0)
			flash("xdg-open failed; the preview is in ~/.cache/themer/fonts");
		else
			flash("preview opened in the browser");
	} else {
		snprintf(text, sizeof text, "preview written to %s", path);
		flash(text);
	}
}

static int
browse(const char *title, char *out, size_t n, int preview)
{
	char filt[64] = "";
	int idx[MAXL], m, sel = 0, vis, off, i, k;

	for (;;) {
		getsize();
		m = 0;
		for (i = 0; i < lstn; i++)
			if (!filt[0] || strcasestr(lst[i], filt))
				idx[m++] = i;
		if (sel >= m)
			sel = m ? m - 1 : 0;
		vis = rows - 13;
		if (vis > m)
			vis = m;
		if (vis < 1)
			vis = 1;
		off = sel - vis / 2;
		if (off > m - vis)
			off = m - vis;
		if (off < 0)
			off = 0;

		frame_begin(preview ? "type to filter   enter choose   ^P preview   esc back"
		                    : "type to filter   enter choose   esc back");
		pbegin(title, vis + 4, PANEL_W);
		{
			char b[160];

			snprintf(b, sizeof b, "filter: %s_", filt);
			pline(ST_INPUT, b);
		}
		pline(ST_NORM, "");
		for (i = 0; i < vis; i++) {
			char b[160];

			snprintf(b, sizeof b, "  %s", lst[idx[off + i]]);
			pline(off + i == sel ? ST_SEL : ST_NORM, b);
		}
		{
			char b[64];

			snprintf(b, sizeof b, "%d of %d", m, lstn);
			pline(ST_DIM, b);
		}
		pbottom();
		frame_end();

		k = readkey();
		switch (k) {
		case K_UP:
			if (sel > 0)
				sel--;
			break;
		case K_DOWN:
			if (sel < m - 1)
				sel++;
			break;
		case K_ENTER:
			if (m) {
				snprintf(out, n, "%s", lst[idx[sel]]);
				return 1;
			}
			break;
		case K_ESC:
			if (filt[0])
				filt[0] = '\0';
			else
				return 0;
			break;
		case K_BS: {
			size_t l = strlen(filt);

			if (l)
				filt[l - 1] = '\0';
			break;
		}
		case K_REDRAW:
			break;
		case '\x10':                       /* ctrl-p: preview without breaking filter typing */
			if (preview && m)
				show_preview(lst[idx[sel]]);
			break;
		default:
			if (k >= 32 && k < 127 && strlen(filt) + 2 < sizeof filt) {
				size_t l = strlen(filt);

				filt[l] = k;
				filt[l + 1] = '\0';
				sel = 0;
			}
			break;
		}
	}
}

/* single-line text field. buf changes only on accept. returns 1 on accept */
static int
input(const char *title, const char *label, char *buf, size_t n)
{
	char b[320];
	size_t len;
	int k;

	snprintf(b, sizeof b, "%s", buf);
	len = strlen(b);
	for (;;) {
		frame_begin("type to edit   enter accept   esc cancel");
		pbegin(title, 5, PANEL_W);
		pline(ST_DIM, label);
		pline(ST_NORM, "");
		pstart(ST_INPUT);
		pput(b);
		fputs("\x1b[7m \x1b[27m", stdout);
		pcol++;
		pend();
		pline(ST_NORM, "");
		pbottom();
		frame_end();

		k = readkey();
		switch (k) {
		case K_ENTER:
			snprintf(buf, n, "%s", b);
			return 1;
		case K_ESC:
			return 0;
		case K_BS:
			if (len)
				b[--len] = '\0';
			break;
		case K_REDRAW:
			break;
		default:
			if (k >= 32 && k < 127 && len + 1 < sizeof b && len + 1 < n) {
				b[len++] = k;
				b[len] = '\0';
			}
			break;
		}
	}
}

/* family plus size. opens the font browser first; + and - set the size.
 * b browses again, t types a name. Esc at the browser cancels the edit. */
static int
font_edit(const char *title, int mono)
{
	char f[NAME_LEN];
	int sz = mono ? cfg->mono_size : cfg->ui_size, k;

	snprintf(f, sizeof f, "%s", mono ? cfg->mono_font : cfg->ui_font);
	if (load_fonts(mono)) {
		if (!browse(mono ? "Monospace fonts" : "Installed fonts", f, sizeof f, 1))
			return 0;
	} else if (!input("Font family", "no fonts found; type a family name", f, sizeof f)) {
		return 0;
	}
	for (;;) {
		char s[32];

		frame_begin("+/- size   b browse   ^P preview   t type   enter accept   esc cancel");
		pbegin(title, 7, PANEL_W);
		pline(ST_DIM, "family");
		pline(ST_INPUT, f);
		pline(ST_NORM, "");
		pline(ST_DIM, "size");
		snprintf(s, sizeof s, "<  %d  >", sz);
		pline(ST_INPUT, s);
		pline(ST_NORM, "");
		pbottom();
		frame_end();

		k = readkey();
		switch (k) {
		case '+':
		case '=':
		case K_RIGHT:
			if (sz < 72)
				sz++;
			break;
		case '-':
		case K_LEFT:
			if (sz > 4)
				sz--;
			break;
		case 'b':
			if (load_fonts(mono))
				browse(mono ? "Monospace fonts" : "Installed fonts", f, sizeof f, 1);
			break;
		case '\x10':
			show_preview(f);
			break;
		case 't':
			input("Font family", "family name", f, sizeof f);
			break;
		case K_ENTER:
			return front_set_font(cfg, mono, f, sz) == 0;
		case 'q':
		case K_ESC:
			return 0;
		}
	}
}

/* ---- screens ---- */

static void
load_theme(const char *name)
{
	if (front_set_theme(cfg, name) < 0)
		flash("cannot load that theme");
}

static void
theme_menu(void)
{
	char names[64][NAME_LEN];
	const char *items[64];
	Theme orig_th = cfg->th;
	int omode = cfg->mode, n, sel = 0, i, vis, off, k;

	n = theme_list(names, 64);
	if (!n) {
		flash("no themes found in ~/.config/themer/themes or the install dir");
		return;
	}
	for (i = 0; i < n; i++) {
		items[i] = names[i];
		if (!strcmp(names[i], orig_th.name))
			sel = i;
	}
	for (;;) {
		char b[96];

		getsize();
		vis = rows - 13;
		if (vis > n)
			vis = n;
		if (vis < 1)
			vis = 1;
		off = sel - vis / 2;
		if (off > n - vis)
			off = n - vis;
		if (off < 0)
			off = 0;

		frame_begin("j/k preview live   m mode   enter keep   esc revert");
		pbegin("themes", vis + 4, PANEL_W);
		for (i = 0; i < vis; i++) {
			snprintf(b, sizeof b, "  %s", items[off + i]);
			pline(off + i == sel ? ST_SEL : ST_NORM, b);
		}
		pline(ST_NORM, "");
		preview_lines();
		pbottom();
		frame_end();

		k = readkey();
		switch (k) {
		case 'j':
		case K_DOWN:
			if (sel < n - 1) {
				sel++;
				load_theme(items[sel]);
			}
			break;
		case 'k':
		case K_UP:
			if (sel > 0) {
				sel--;
				load_theme(items[sel]);
			}
			break;
		case 'm':
			cfg->mode ^= 1;
			break;
		case K_ENTER:
			if (strcmp(cfg->th.name, orig_th.name) || cfg->mode != omode)
				dirty = 1;
			return;
		case 'q':
		case K_ESC:
			cfg->th = orig_th;
			cfg->mode = omode;
			return;
		}
	}
}

static int
parse_color(const char *s, char *out)
{
	const char *h = s[0] == '#' ? s + 1 : s;

	if (!*h) {
		out[0] = '\0';
		return 1;
	}
	if (strlen(h) != 6)
		return 0;
	for (const char *p = h; *p; p++)
		if (!isxdigit((unsigned char)*p))
			return 0;
	snprintf(out, TM_COLOR_LEN, "%s", h);
	return 1;
}

static void
colors_menu(void)
{
	int sel = 0, r;

	for (;;) {
		char it[C_N][96];
		const char *items[C_N];
		int i;

		for (i = 0; i < C_N; i++) {
			snprintf(it[i], sizeof it[i], "%-8s  %s%s", color_names[i], sc.hex[i],
			         cfg->color[i][0] ? "  (override)" : "");
			items[i] = it[i];
		}
		r = pick("Colors", "override single colors. theme values apply otherwise",
		         items, C_N, &sel, "enter edit   x clear override   q back");
		if (r == -1)
			return;
		if (r == -2) {
			if (last_key == 'x') {
				cfg->color[sel][0] = '\0';
				dirty = 1;
			}
			continue;
		}
		if (r >= 0) {
			char buf[TM_COLOR_LEN], ov[TM_COLOR_LEN];

			snprintf(buf, sizeof buf, "%s", sc.hex[r]);
			if (!input("Edit color", color_names[r], buf, sizeof buf))
				continue;
			if (!parse_color(buf, ov)) {
				flash("expected 6 hex digits, e.g. 5f87af (empty clears)");
				continue;
			}
			snprintf(cfg->color[r], TM_COLOR_LEN, "%s", ov);
			dirty = 1;
		}
	}
}

/* parses NAME=VALUE; NAME= deletes */
static int
env_line(const char *line)
{
	const char *eq = strchr(line, '=');
	char name[64];
	size_t k;

	if (!eq || eq == line) {
		flash("format: NAME=VALUE   (NAME= deletes)");
		return 0;
	}
	k = eq - line;
	if (k >= sizeof name) {
		flash("name too long");
		return 0;
	}
	memcpy(name, line, k);
	name[k] = '\0';
	env_set(cfg, name, eq + 1);
	return 1;
}

static void
view_environ(void)
{
	static const char *ev[512];
	int n = 0, sel = 0;

	for (char **e = environ; *e && n < 512; e++)
		ev[n++] = *e;
	if (!n)
		return;
	while (pick("Process environment", "read only", ev, n, &sel, "j/k move   q back") != -1)
		;
}

static void
env_menu(void)
{
	int sel = 0, r;

	for (;;) {
		char it[MAX_ENV + 2][320];
		const char *items[MAX_ENV + 2];
		int i, n = 0;

		for (i = 0; i < cfg->nenv; i++) {
			snprintf(it[n], sizeof it[n], "%s=%s", cfg->env[i].name, cfg->env[i].value);
			items[n] = it[n];
			n++;
		}
		items[n++] = "+ new variable";
		items[n++] = "> view process environment";

		r = pick("Environment", "applied through environment.d on apply",
		         items, n, &sel, "enter edit   n new   d delete   q back");
		if (r == -1)
			return;
		if (r == -2) {
			if (last_key == 'n') {
				char buf[320] = "";

				if (input("New variable", "NAME=VALUE", buf, sizeof buf) && env_line(buf))
					dirty = 1;
			} else if (last_key == 'd' && sel < cfg->nenv) {
				char nm[64];

				snprintf(nm, sizeof nm, "%s", cfg->env[sel].name);
				env_set(cfg, nm, "");
				dirty = 1;
			}
			continue;
		}
		if (r < cfg->nenv) {
			char buf[320];

			snprintf(buf, sizeof buf, "%s=%s", cfg->env[r].name, cfg->env[r].value);
			if (input("Edit variable", "NAME=VALUE", buf, sizeof buf) && env_line(buf))
				dirty = 1;
		} else if (r == cfg->nenv) {
			char buf[320] = "";

			if (input("New variable", "NAME=VALUE", buf, sizeof buf) && env_line(buf))
				dirty = 1;
		} else {
			view_environ();
		}
	}
}

/* the value after cur in a "a|b|c" list, wrapping around; the first if cur is not listed */
static void
next_choice(const char *choices, const char *cur, char *out, size_t n)
{
	const char *p = choices, *first = NULL, *found = NULL;

	while (*p) {
		const char *bar = strchr(p, '|');
		size_t len = bar ? (size_t)(bar - p) : strlen(p);
		char item[64];

		snprintf(item, sizeof item, "%.*s", (int)(len < 63 ? len : 63), p);
		if (!first)
			first = p;
		if (found) {
			snprintf(out, n, "%s", item);
			return;
		}
		if (!strcmp(item, cur))
			found = p;
		if (!bar)
			break;
		p = bar + 1;
	}
	snprintf(out, n, "%.*s", (int)strcspn(first, "|"), first);
}

/* sets one option of a module; reports a bad value in the status line */
static void
set_option(int mod, int opt, const char *value)
{
	char msg[256];

	if (front_option_set(cfg, mod, opt, value, msg, sizeof msg) < 0)
		flash(msg);
}

/* the options a module offers: shows each value, changes it on enter */
/* One line per option of a module: name, value, and the choices. */
/* The values an option accepts, as the option pane shows them. */
static void
option_range(const TmOption *o, char *out, size_t n)
{
	if (o->choices)
		snprintf(out, n, "choices: %s", o->choices);
	else if (o->max > o->min)
		snprintf(out, n, "number from %d to %d", o->min, o->max);
	else
		snprintf(out, n, "free text");
}

/* Two panes: modules on the left, the selected module's options on the right.
 * j/k move within a pane, h/l (or the arrows) switch panes, enter acts:
 * on a module it loads or unloads it, on an option it changes the value. */
static void
modules_menu(void)
{
	int order[MAX_MODULES];
	int n = front_module_order(order, MAX_MODULES), sel = 0, osel = 0, focus = 0, k;

	if (!n) {
		flash("no modules found");
		return;
	}
	for (;;) {
		int vis, off, i, no, inner, pane, total, left, top, j;
		char b[256];

		getsize();
		no = themer_option_count(order[sel]);
		vis = rows - 10;
		if (vis > n)
			vis = n;
		if (vis < 1)
			vis = 1;
		off = sel - vis / 2;
		if (off > n - vis)
			off = n - vis;
		if (off < 0)
			off = 0;
		inner = vis > no + 3 ? vis : no + 3;
		pane = (cols - 6) / 2;
		if (pane > PANEL_W / 2 + 14)
			pane = PANEL_W / 2 + 14;
		if (pane < 22)
			pane = 22;
		total = 2 * pane + 2;
		left = (cols - total) / 2 + 1;
		if (left < 1)
			left = 1;
		top = (rows - (inner + 2)) / 2 + 1;
		if (top < 3)
			top = 3;

		frame_begin(focus ? "j/k move   enter change   h back to modules   q back"
		                  : "j/k move   enter load/unload   l options   q back");

		pbegin_at("modules", inner, pane, left, top);
		for (i = 0; i < vis; i++) {
			snprintf(b, sizeof b, "%c [%c] %s%s", off + i == sel ? '>' : ' ',
			         themer_module_on(cfg, order[off + i]) ? 'x' : ' ',
			         themer_module_name(order[off + i]),
			         themer_module_present(cfg, order[off + i]) ? "" : "  (not installed)");
			pline(off + i == sel && !focus ? ST_SEL : ST_NORM, b);
		}
		for (; i < inner; i++)
			pline(ST_NORM, "");
		pbottom();

		pbegin_at(no ? "options" : "options (none)", inner, pane, left + pane + 2, top);
		snprintf(b, sizeof b, "%s: %s", themer_module_name(order[sel]),
		         themer_module_on(cfg, order[sel]) ? "loaded" : "not loaded");
		pline(ST_DIM, b);
		for (j = 0; j < no; j++) {
			const TmOption *o = themer_option(order[sel], j);

			snprintf(b, sizeof b, "  %-10s %s", o->name, themer_option_value(cfg, order[sel], j));
			pline(focus && j == osel ? ST_SEL : ST_NORM, b);
		}
		if (!no)
			pline(ST_DIM, "  this module has no options");
		else {
			const TmOption *o = themer_option(order[sel], osel);

			pline(ST_NORM, "");
			snprintf(b, sizeof b, "%s", o->desc);
			pline(ST_DIM, b);
			option_range(o, b, sizeof b);
			pline(ST_DIM, b);
		}
		pbottom();
		frame_end();

		k = readkey();
		switch (k) {
		case 'j':
		case K_DOWN:
			if (!focus && sel < n - 1) {
				sel++;
				osel = 0;
			} else if (focus && osel < no - 1)
				osel++;
			break;
		case 'k':
		case K_UP:
			if (!focus && sel > 0) {
				sel--;
				osel = 0;
			} else if (focus && osel > 0)
				osel--;
			break;
		case 'l':
		case K_RIGHT:
			if (no)
				focus = 1;
			else
				flash("this module has no options");
			break;
		case 'h':
		case K_LEFT:
			focus = 0;
			break;
		case K_ENTER:
			if (focus) {
				const TmOption *o = themer_option(order[sel], osel);
				const char *cur = themer_option_value(cfg, order[sel], osel);
				char next[NAME_LEN], buf[NAME_LEN];

				if (o->choices) {
					next_choice(o->choices, cur, next, sizeof next);
					set_option(order[sel], osel, next);
				} else {
					snprintf(buf, sizeof buf, "%s", cur);
					if (input(o->name, o->desc, buf, sizeof buf))
						set_option(order[sel], osel, buf);
				}
			} else {
				int on = themer_module_on(cfg, order[sel]);

				if (themer_module_set(cfg, order[sel], !on) < 0)
					flash(on ? "unload had errors" : "load failed (see stderr)");
				else
					flash(on ? "module unloaded" : "module loaded");
				state_save(cfg);
			}
			break;
		case 'q':
		case K_ESC:
			if (focus)
				focus = 0;
			else
				return;
			break;
		}
	}
}

/* the template set, picked from every set found in the template directories */
/* Shows a report (the output of set check, import, or copy, captured in a temporary
 * file) as a scrollable list. */
static void
show_report(const char *title, const char *path)
{
	static char lines[256][192];
	const char *items[256];
	char buf[512];
	FILE *f = fopen(path, "r");
	int n = 0, sel = 0;

	if (!f)
		return;
	while (n < 256 && fgets(buf, sizeof buf, f)) {
		buf[strcspn(buf, "\r\n")] = '\0';
		snprintf(lines[n], sizeof lines[n], "%.*s", (int)sizeof lines[n] - 1, buf);
		items[n] = lines[n];
		n++;
	}
	fclose(f);
	if (!n) {
		flash("nothing to show");
		return;
	}
	while (pick(title, "", items, n, &sel, "j/k scroll   q back") != -1)
		;
}

/* runs fn with its report going to a temporary file, then shows it */
static int
with_report(const char *title, int (*fn)(FILE *, void *), void *arg)
{
	char path[PATH_MAX];
	FILE *f;
	int r;

	path_under_home(path, sizeof path, ".cache/themer/report.txt");
	if (write_bytes(path, "", 0) < 0 || !(f = fopen(path, "w")))
		return -1;
	r = fn(f, arg);
	fclose(f);
	show_report(title, path);
	return r;
}

static int do_check(FILE *f, void *arg) { return set_check(cfg, arg, f); }
static int do_copy(FILE *f, void *arg) { return front_set_copy(((char **)arg)[0], ((char **)arg)[1], f) < 0 ? -1 : 0; }
static int
do_import(FILE *f, void *arg)
{
	char **a = arg;

	return set_import(cfg, a[0], a[1], a[2], f);
}

/* a new set: a copy of the selected one under a new name, to edit */
static void
set_new(const char *from)
{
	char name[NAME_LEN] = "";
	const char *args[2];

	if (!input("New template set", "name for the copy (letters, digits, - and _)", name, sizeof name) || !name[0])
		return;
	args[0] = from;
	args[1] = name;
	if (with_report("set copy", do_copy, args) < 0) {
		flash("could not copy; the name may be taken or not valid");
		return;
	}
	if (front_set_set(cfg, name) == 0)
		dirty = 1;
}

/* import an existing theme into a set: the set's name, the module, the path */
static void
set_import_tui(const char *into)
{
	char name[NAME_LEN], module[NAME_LEN] = "gtk", path[PATH_MAX] = "";
	const char *mods[MAX_MODULES], *args[3];
	int nm = 0, sel = 0, r;

	snprintf(name, sizeof name, "%s", strcmp(into, TM_DEFAULT_SET) ? into : "");
	if (!input("Import into set", "name of the set to add to (new or existing)", name, sizeof name) || !name[0])
		return;
	for (int i = 0; i < themer_module_count(); i++)
		if (themer_module_templates(i))
			mods[nm++] = themer_module_name(i);
	r = pick("Module", "whose files are you importing?", mods, nm, &sel, "j/k move   enter select   esc back");
	if (r < 0)
		return;
	snprintf(module, sizeof module, "%s", mods[r]);
	if (!input("Path", "the theme: a file or a directory (a GTK theme dir, a chrome/ dir, a .qss)", path, sizeof path) || !path[0])
		return;
	args[0] = name;
	args[1] = module;
	args[2] = path;
	if (with_report("set import", do_import, args) < 0) {
		flash("import failed; see the report");
		return;
	}
	if (front_set_set(cfg, name) == 0)
		dirty = 1;
}

static void
set_pick(void)
{
	char names[MAX_NAMES][NAME_LEN];
	const char *items[MAX_NAMES];
	int n, sel = 0, r;

	for (;;) {
		n = template_all_sets(names, MAX_NAMES);
		if (!n) {
			flash("no template sets found");
			return;
		}
		for (int i = 0; i < n; i++) {
			items[i] = names[i];
			if (!strcmp(names[i], cfg->set))
				sel = i;
		}
		r = pick("template sets", "the files every module renders from; what a set lacks comes from themer_chicago",
		         items, n, &sel, "enter select   n new (copy)   i import a theme   c check   esc back");
		if (r == -1)
			return;
		if (r >= 0) {
			if (strcmp(cfg->set, items[r]) && front_set_set(cfg, items[r]) == 0)
				dirty = 1;
			return;
		}
		if (last_key == 'n')
			set_new(items[sel]);
		else if (last_key == 'i')
			set_import_tui(items[sel]);
		else if (last_key == 'c')
			with_report("set check", do_check, (void *)items[sel]);
	}
}

static void
tty_pick(void)
{
	static const char *dirs[] = {
		"/usr/share/kbd/consolefonts", "/usr/share/consolefonts", "/usr/lib/kbd/consolefonts",
	};
	char out[NAME_LEN];

	lstn = 0;
	for (size_t i = 0; i < TM_COUNT(dirs); i++)
		load_dir(dirs[i], 1, 0);
	finish_list();
	if (!lstn) {
		if (input("TTY font", "console font name", cfg->tty_font, sizeof cfg->tty_font))
			dirty = 1;
		return;
	}
	if (browse("TTY console font", out, sizeof out, 0)) {
		snprintf(cfg->tty_font, sizeof cfg->tty_font, "%s", out);
		dirty = 1;
	}
}

/* icon or cursor themes, from the icon directories (icons.c) */
static void
icon_pick(int cursors)
{
	static char names[MAX_NAMES][NAME_LEN];
	char out[NAME_LEN];
	const char *dst = cursors ? cfg->cursor : cfg->icons;
	int n = icon_themes_scan(names, MAX_NAMES, cursors);

	lstn = 0;
	for (int i = 0; i < n && lstn < MAXL; i++)
		snprintf(lst[lstn++], sizeof lst[0], "%s", names[i]);
	if (!lstn) {
		snprintf(out, sizeof out, "%s", dst);
		if (!input(cursors ? "Cursor theme" : "Icon theme", "theme name", out, sizeof out))
			return;
	} else if (!browse(cursors ? "Cursor themes" : "Icon themes", out, sizeof out, 0)) {
		return;
	}
	if ((cursors ? front_set_cursor(cfg, out, 0) : front_set_icons(cfg, out)) == 0)
		dirty = 1;
	else
		flash("that name is not valid");
}

static void
cursor_size_edit(void)
{
	char buf[16];

	snprintf(buf, sizeof buf, "%d", cfg->cursor_size);
	if (input("Cursor size", "pixels (24, 32, 48...)", buf, sizeof buf) && atoi(buf) != cfg->cursor_size) {
		if (atoi(buf) > 0 && front_set_cursor(cfg, NULL, atoi(buf)) == 0)
			dirty = 1;
		else
			flash("the size is a number of pixels");
	}
}

/* font rendering: hinting, antialiasing, subpixel order. Applied through the GTK
 * settings, xsettingsd, and gsettings (the gtk module) and fontconfig. */
static void
render_menu(void)
{
	static const char *labels[] = { "Hinting", "Antialiasing", "Subpixel order" };
	const char *items[3];
	char val[3][64];
	int sel = 0, r;

	for (;;) {
		snprintf(val[0], sizeof val[0], "  %-16s%s", labels[0], cfg->hinting);
		snprintf(val[1], sizeof val[1], "  %-16s%s", labels[1], cfg->antialias ? "on" : "off");
		snprintf(val[2], sizeof val[2], "  %-16s%s", labels[2], cfg->rgba);
		for (int i = 0; i < 3; i++)
			items[i] = val[i];
		r = pick("Font rendering", "how text is drawn; GTK, Qt, and X apps follow",
		         items, 3, &sel, "j/k move   enter change   esc back");
		if (r < 0)
			return;
		if (r == 0) {
			int s = 0, k;

			for (int i = 0; i < 4; i++)
				if (!strcmp(hinting_values[i], cfg->hinting))
					s = i;
			k = pick("Hinting", "slight suits most screens; full is crisp but uneven",
			         (const char **)hinting_values, 4, &s, "j/k move   enter select   esc back");
			if (k >= 0 && strcmp(cfg->hinting, hinting_values[k]) &&
			    front_set_hinting(cfg, hinting_values[k]) == 0)
				dirty = 1;
		} else if (r == 1) {
			front_set_antialias(cfg, !cfg->antialias);
			dirty = 1;
		} else {
			int s = 0, k;

			for (int i = 0; i < 5; i++)
				if (!strcmp(rgba_values[i], cfg->rgba))
					s = i;
			k = pick("Subpixel order", "rgb for most LCDs; none for HiDPI or rotated screens",
			         (const char **)rgba_values, 5, &s, "j/k move   enter select   esc back");
			if (k >= 0 && strcmp(cfg->rgba, rgba_values[k]) && front_set_rgba(cfg, rgba_values[k]) == 0)
				dirty = 1;
		}
	}
}

/* Applies everything with stderr sent to ~/.cache/themer/errors.log, so the
 * messages do not draw over the screen. The first line is shown in the status. */
static void
apply_all(void)
{
	char dir[PATH_MAX], log[PATH_MAX], first[160] = "";
	int saved, fd, r;
	FILE *f;

	path_under_home(dir, sizeof dir, ".cache");
	mkdir(dir, 0755);
	path_under_home(dir, sizeof dir, ".cache/themer");
	mkdir(dir, 0755);
	path_under_home(log, sizeof log, ".cache/themer/errors.log");

	fflush(stderr);
	saved = dup(STDERR_FILENO);
	fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0)
		dup2(fd, STDERR_FILENO);
	r = front_apply(cfg);
	fflush(stderr);
	if (fd >= 0) {
		dup2(saved, STDERR_FILENO);
		close(fd);
	}
	close(saved);

	if (r && (f = fopen(log, "r"))) {
		if (fgets(first, sizeof first, f))
			first[strcspn(first, "\r\n")] = '\0';
		fclose(f);
	}
	if (r)
		flash(first[0] ? first : "applied with errors");
	else
		flash("applied and saved");
	dirty = 0;
}

static void
toggle_mode(void)
{
	cfg->mode ^= 1;
	dirty = 1;
}

static int
modules_on(void)
{
	int i, n = 0;

	for (i = 0; i < themer_module_count(); i++)
		if (themer_module_on(cfg, i))
			n++;
	return n;
}

static int
count_overrides(void)
{
	int i, n = 0;

	for (i = 0; i < C_N; i++)
		if (cfg->color[i][0])
			n++;
	return n;
}

/* returns 0 when quit was chosen */
static int
main_action(int sel)
{
	switch (sel) {
	case 0: theme_menu(); break;
	case 1: toggle_mode(); break;
	case 2: set_pick(); break;
	case 3: if (font_edit("UI font", 0)) dirty = 1; break;
	case 4: if (font_edit("Terminal font", 1)) dirty = 1; break;
	case 5: tty_pick(); break;
	case 6: icon_pick(0); break;
	case 7: icon_pick(1); break;
	case 8: cursor_size_edit(); break;
	case 9: render_menu(); break;
	case 10: colors_menu(); break;
	case 11: env_menu(); break;
	case 12: modules_menu(); break;
	case 13: apply_all(); break;
	default: return 0;
	}
	return 1;
}

static void
main_menu(void)
{
	static const char *labels[] = {
		"Theme", "Mode", "Template set", "UI font", "Terminal font", "TTY font", "Icon theme",
		"Cursor theme", "Cursor size", "Font rendering",
		"Colors", "Environment", "Modules", "Apply", "Quit",
	};
	enum { N = TM_COUNT(labels) };
	int sel = 0, armed = 0, k;

	for (;;) {
		char val[N][128], line[160];
		int i;

		snprintf(val[0], sizeof val[0], "%s", cfg->th.name);
		snprintf(val[1], sizeof val[1], "%s", cfg->mode == MODE_DARK ? "dark" : "light");
		snprintf(val[2], sizeof val[2], "%s", cfg->set);
		snprintf(val[3], sizeof val[3], "%s  %d", cfg->ui_font, cfg->ui_size);
		snprintf(val[4], sizeof val[4], "%s  %d", cfg->mono_font, cfg->mono_size);
		snprintf(val[5], sizeof val[5], "%s", cfg->tty_font);
		snprintf(val[6], sizeof val[6], "%s", cfg->icons);
		snprintf(val[7], sizeof val[7], "%s", cfg->cursor);
		snprintf(val[8], sizeof val[8], "%d", cfg->cursor_size);
		snprintf(val[9], sizeof val[9], "%s, %s, %s", cfg->hinting,
		         cfg->antialias ? "antialiased" : "aliased", cfg->rgba);
		snprintf(val[10], sizeof val[10], "%d override(s)", count_overrides());
		snprintf(val[11], sizeof val[11], "%d variable(s)", cfg->nenv);
		snprintf(val[12], sizeof val[12], "%d of %d on", modules_on(), themer_module_count());
		snprintf(val[13], sizeof val[13], "%s", dirty ? "unapplied changes" : "up to date");

		frame_begin("j/k move   enter select   m mode   a apply   q quit");
		pbegin("settings", N + 4, PANEL_W);
		for (i = 0; i < N - 1; i++) {
			snprintf(line, sizeof line, "  %-15s%s", labels[i], val[i]);
			pline(i == sel ? ST_SEL : ST_NORM, line);
		}
		pline(sel == N - 1 ? ST_SEL : ST_NORM, "  Quit");
		pline(ST_NORM, "");
		preview_lines();
		pbottom();
		frame_end();

		k = readkey();
		if (k != 'q' && k != K_ESC)
			armed = 0;
		switch (k) {
		case 'j':
		case K_DOWN:
			if (sel < N - 1)
				sel++;
			break;
		case 'k':
		case K_UP:
			if (sel > 0)
				sel--;
			break;
		case 'm':
			toggle_mode();
			break;
		case 'a':
			apply_all();
			break;
		case 'q':
		case K_ESC:
			if (dirty && !armed) {
				flash("unapplied changes. press q again to quit, or a to apply");
				armed = 1;
				break;
			}
			return;
		case K_ENTER:
			if (!main_action(sel))
				return;
			break;
		}
	}
}

void
tui_run(Config *c)
{
	cfg = c;
	term_on();
	main_menu();
	term_off();
}
