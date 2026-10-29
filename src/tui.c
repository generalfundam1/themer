/* Terminal UI. The whole screen is drawn with the active theme, so a
 * theme change shows up immediately in the interface itself. */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
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
#define PANEL_W  72
#define MOD_HEAD 2     /* the module's description, in the modules screen */

extern char **environ;

static Config *cfg;
static Scheme sc;
static struct termios orig;
static int rows = 24, cols = 80, dirty, last_key;
static int ptop, pleft, pw, prow, pcol;
/* A panel taller than the room between the title bar and the hint line scrolls: pskip
 * of its lines are above the room, pvis fit, and the line pfocus (the selection, when
 * the caller sets it before pbegin) is kept in view. Lines out of view are written to
 * sink, so they never land on the hint line or the title bar. */
static int pskip, pvis = 1 << 30, pfocus = -1;
/* the settings as last saved, for front_sync to merge against */
static Config base;
static FILE *term, *sink;  /* where drawing goes: the terminal, or nowhere (sink) */
static char status[160];  /* the message on the status line */
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
	fprintf(term, "\x1b[38;2;%d;%d;%dm", r, g, b);
}

static void
setbg(const char *h)
{
	int r, g, b;

	rgb_of(h, &r, &g, &b);
	fprintf(term, "\x1b[48;2;%d;%d;%dm", r, g, b);
}

static void
at(int r, int c)
{
	fprintf(term, "\x1b[%d;%dH", r, c);
}

static void
flash(const char *s)
{
	snprintf(status, sizeof status, "%s", s);
}

/* ---- terminal setup ---- */

static void
term_off(void)
{
	fputs("\x1b[0m\x1b[?25h\x1b[?1049l", term);
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

	term = stdout;
	if (!sink)
		sink = fopen("/dev/null", "w");
	if (!sink)
		sink = stdout;              /* no /dev/null: overdraw rather than crash */
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
	fputs("\x1b[?1049h\x1b[?25l", term);
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

/* takes in what another front end saved, keeping the changes not applied here */
static void
tui_sync(void)
{
	if (front_sync()) {
		dirty = !config_same(cfg, &base);
		flash(front_text(FT_SYNCED));
	}
}

static int
readkey(void)
{
	struct termios t, raw;
	int c, a, b;

	/* waits for a key, but wakes once a second: when another front end saved, the
	 * screen is redrawn with what it changed */
	for (;;) {
		struct pollfd p = { STDIN_FILENO, POLLIN, 0 };
		int r = poll(&p, 1, 1000);

		if (r > 0)
			break;
		if (r < 0 && errno != EINTR)
			return K_ESC;
		if (r < 0 || state_changed()) {     /* a resize, or the state file */
			status[0] = '\0';
			tui_sync();
			return K_REDRAW;
		}
	}
	c = rd();

	status[0] = '\0';                 /* a message shows for one frame */
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
	fputs("\x1b[1m", term);
	fprintf(term, "%.*s", lw, left);
	pad = max - lw - rw;
	fprintf(term, "%*s", pad, "");
	fprintf(term, "%.*s", rw, right);
	fputs("\x1b[22m", term);
}

static void
frame_begin(const char *hint)
{
	char right[160];
	int r;

	tui_sync();
	getsize();
	theme_scheme(cfg, &sc);
	fputs("\x1b[0m\x1b[H", term);
	for (r = 1; r <= rows; r++) {
		at(r, 1);
		setbg(sc.hex[C_BG]);
		fprintf(term, "%*s", cols - 1, "");
	}
	snprintf(right, sizeof right, "%s  %s%s ", cfg->th.name,
	         cfg->mode == MODE_DARK ? "dark" : "light", dirty ? "  *" : "");
	bar(1, sc.hex[C_ACC], sc.hex[C_ACCFG], " themer", right);
	bar(rows, sc.hex[C_SURF], sc.hex[C_FG], hint, "");
}

static void
frame_end(void)
{
	if (status[0]) {
		int len = strlen(status), col = (cols - len) / 2 + 1;

		if (col < 1)
			col = 1;
		at(rows - 1, col);
		setbg(sc.hex[C_BG]);
		setfg(sc.hex[C_WARN]);
		fputs(status, term);
	}
	fflush(stdout);
}

/* ---- panel: a centered card. pbegin/pline/pbottom draw it line by line ---- */

static void
style(int st)
{
	fputs("\x1b[22m", term);
	switch (st) {
	case ST_SEL:   setbg(sc.hex[C_ACC]);  setfg(sc.hex[C_ACCFG]); break;
	case ST_DIM:   setbg(sc.hex[C_SURF]); setfg(sc.ansi[8]); break;
	case ST_INPUT: setbg(sc.hex[C_BG]);   setfg(sc.hex[C_FG]); break;
	default:       setbg(sc.hex[C_SURF]); setfg(sc.hex[C_FG]); break;
	}
}

/* opens a panel whose top-left corner is at (top, left) */
/* positions the cursor for the panel line prow, or sends the line to sink when it is
 * scrolled out of view */
static void
prow_at(void)
{
	int li = prow - ptop;

	term = li >= pskip && li < pskip + pvis ? stdout : sink;
	at(ptop + li - pskip, pleft);
}

static void
pbegin_at(const char *title, int inner, int w, int left, int top)
{
	int k, tl, h = inner + 2, room;
	char t[64];

	if (top < 2)
		top = 2;
	room = rows - 1 - top;             /* from top down to the line above the hint */
	if (h > room) {
		/* too tall: right under the title bar, scrolled to the focus */
		top = 2;
		room = rows - 3 > 3 ? rows - 3 : 3;
		pvis = room;
		pskip = pfocus < 0 ? 0 : pfocus - room / 2;
		if (pskip > h - room)
			pskip = h - room;
		if (pskip < 0)
			pskip = 0;
	} else {
		pskip = 0;
		pvis = h;
	}
	pfocus = -1;
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
	prow_at();
	setbg(sc.hex[C_SURF]);
	setfg(sc.hex[C_ACC]);
	fputs("┌─ ", term);
	fputs("\x1b[1m", term);
	setfg(sc.hex[C_FG]);
	fprintf(term, "%s", t);
	fputs("\x1b[22m", term);
	setfg(sc.hex[C_ACC]);
	fputs(" ", term);
	for (k = 0; k < pw - tl - 3; k++)
		fputs("─", term);
	fputs("┐", term);
	prow++;
	term = stdout;
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
		top = 3;   /* pbegin_at moves it up and scrolls when it does not fit */
	if (left < 1)
		left = 1;
	pbegin_at(title, inner, width, left, top);
}

static void
pstart(int st)
{
	prow_at();
	setbg(sc.hex[C_SURF]);
	setfg(sc.hex[C_ACC]);
	fputs("│", term);
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
	fprintf(term, "%.*s", n, s);
	pcol += n;
}

static void
pend(void)
{
	while (pcol < pw) {
		putc(' ', term);
		pcol++;
	}
	setbg(sc.hex[C_SURF]);
	setfg(sc.hex[C_ACC]);
	fputs("│", term);
	prow++;
	term = stdout;
}

static void
pline(int st, const char *s)
{
	pstart(st);
	pput(s);
	pend();
}

/* text over n dim lines w wide, broken at spaces; always n lines */
static void
note_lines(const char *text, int n, int w)
{
	char b[256];

	if (w < 16)
		w = 16;
	for (int k = 0; k < n; k++) {
		int len = (int)strlen(text), cut = len;

		if (len > w) {
			cut = w;
			while (cut > w / 2 && text[cut] != ' ')
				cut--;
			if (text[cut] != ' ')
				cut = w;
		}
		if (k == n - 1 && len > cut)   /* the last line: what is left, cut with ... */
			snprintf(b, sizeof b, "  %.*s...", w - 3 > 0 ? w - 3 : 0, text);
		else
			snprintf(b, sizeof b, "  %.*s", cut, text);
		pline(ST_DIM, b);
		text += cut;
		while (*text == ' ')
			text++;
	}
}

static void
pbottom(void)
{
	int k;

	prow_at();
	setbg(sc.hex[C_SURF]);
	setfg(sc.hex[C_ACC]);
	fputs("└", term);
	for (k = 0; k < pw; k++)
		fputs("─", term);
	fputs("┘", term);
	term = stdout;
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
		fputs("██", term);
		pcol += 2;
	}
	pend();
	pstart(ST_NORM);
	for (i = 8; i < 16; i++) {
		setfg(sc.ansi[i]);
		fputs("██", term);
		pcol += 2;
	}
	pend();
	pstart(ST_NORM);
	for (i = 0; i < 7; i++) {
		setfg(sc.hex[sem[i]]);
		fputs("██", term);
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
		if (front_open(path) != 0)
			flash("xdg-open failed; the preview is in ~/.config/themer/cache/fonts");
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
		fputs("\x1b[7m \x1b[27m", term);
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

/* ---- screens ---- */

static void
load_theme(const char *name)
{
	if (front_set_theme(cfg, name) < 0)
		flash("cannot load that theme");
}

static int with_report(const char *title, int (*fn)(FILE *, void *), void *arg);

static int
import_scheme_cb(FILE *f, void *path)
{
	return scheme_import(path, NULL, f) > 0 ? 0 : -1;
}

static void
theme_menu(void)
{
	static char names[MAX_THEMES][NAME_LEN];
	static const char *items[MAX_THEMES];
	Theme orig_th = cfg->th;
	int omode = cfg->mode, n, sel = 0, i, vis, off, k;
	char armed_rm[NAME_LEN] = "";

	n = theme_list(names, MAX_THEMES);
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
		char path[PATH_MAX], what[NAME_LEN];
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

		frame_begin("j/k preview   / find   m mode   i import   d remove   r restore   enter keep   esc revert");
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
		if (k != 'd')
			armed_rm[0] = '\0';
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
		case 'g':
		case 'G':
			sel = k == 'g' ? 0 : n - 1;
			load_theme(items[sel]);
			break;
		case '/':
			what[0] = '\0';
			if (input(front_text(FT_SCHEME_FIND), "part of its name", what, sizeof what) && what[0])
				for (i = 1; i <= n; i++)
					if (strstr(items[(sel + i) % n], what)) {
						sel = (sel + i) % n;
						load_theme(items[sel]);
						break;
					}
			break;
		case 'i':
			/* a scheme file: .theme, Gogh lines, Xresources, kitty, foot, alacritty */
			path[0] = '\0';
			if (!input(front_text(FT_SCHEME_IMPORT_TITLE), front_text(FT_SCHEME_IMPORT_TIP),
			           path, sizeof path) || !path[0])
				break;
			with_report("scheme import", import_scheme_cb, path);
			n = theme_list(names, MAX_THEMES);
			for (i = 0; i < n; i++)
				items[i] = names[i];
			if (sel >= n)
				sel = n - 1;
			break;
		case 'd':
			/* asked first, as in the other front ends: d again removes it */
			if (strcmp(armed_rm, items[sel])) {
				char ask[NAME_LEN + 160];

				snprintf(armed_rm, sizeof armed_rm, "%s", items[sel]);
				snprintf(ask, sizeof ask, "%s %s? d again: %s", front_text(FT_SCHEME_REMOVE), items[sel],
				         front_text(FT_SCHEME_REMOVE_ASK));
				flash(ask);
				break;
			}
			armed_rm[0] = '\0';
			snprintf(what, sizeof what, "%s", items[sel]);
			{
				Config in_use = { 0 };
				char msg[256];

				in_use.th = base.th;   /* the applied one, as in the other front ends */
				front_scheme_remove(&in_use, what, msg, sizeof msg);
				flash(msg);
			}
			n = theme_list(names, MAX_THEMES);
			for (i = 0; i < n; i++)
				items[i] = names[i];
			if (sel >= n)
				sel = n - 1;
			load_theme(items[sel]);
			break;
		case 'r':
			if (scheme_restore(NULL) == 0)
				flash(front_text(FT_SCHEME_RESTORED));
			n = theme_list(names, MAX_THEMES);
			for (i = 0; i < n; i++) {
				items[i] = names[i];
				if (!strcmp(names[i], cfg->th.name))
					sel = i;
			}
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
		r = pick(front_text(FT_COLORS), front_text(FT_COLORS_NOTE),
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
	{
		char msg[160];

		if (front_env_set(cfg, name, eq + 1, msg, sizeof msg) < 0) {
			flash(msg);
			return 0;
		}
	}
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

		r = pick(front_text(FT_ENV), front_text(FT_ENV_NOTE),
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

/* An option's value as the TUI draws it, the same controls as the windows draw:
 * [x] on for a switch, the choices side by side with the chosen one in
 * brackets, < 2 > for a number, the set's title for the template. */
static void
option_shown(int mod, int opt, char *out, size_t n)
{
	const TmOption *o = themer_option(mod, opt);
	const char *v = themer_option_value(cfg, mod, opt);
	size_t len = 0;

	switch (front_option_control(mod, opt)) {
	case OC_SWITCH:
		snprintf(out, n, "[%c] %s", strcmp(v, "on") ? ' ' : 'x', v);
		return;
	case OC_BUTTONS:
		out[0] = '\0';
		for (const char *p = o->choices; *p && len + 1 < n; ) {
			size_t k = strcspn(p, "|");
			int on = strlen(v) == k && !strncmp(p, v, k);

			len += (size_t)snprintf(out + len, n - len, on ? "[%.*s] " : " %.*s  ", (int)k, p);
			p += k;
			if (*p)
				p++;
		}
		return;
	case OC_SLIDER:
		snprintf(out, n, "< %s >", v);
		return;
	case OC_LIST:
		if (!strcmp(o->name, "template")) {
			char t[NAME_LEN];

			snprintf(out, n, "%s", front_set_title(v, t, sizeof t));
			return;
		}
		/* fall through */
	default:
		snprintf(out, n, "%s", v);
	}
}

/* steps an option one way: the next or previous choice, a number up or down, a
 * switch flipped */
static void
option_step(int mod, int opt, int dir)
{
	const TmOption *o = themer_option(mod, opt);
	const char *cur = themer_option_value(cfg, mod, opt);
	char v[NAME_LEN];

	switch (front_option_control(mod, opt)) {
	case OC_SWITCH:
		set_option(mod, opt, strcmp(cur, "on") ? "on" : "off");
		return;
	case OC_SLIDER: {
		int k = atoi(cur) + dir;

		if (k >= o->min && k <= o->max) {
			snprintf(v, sizeof v, "%d", k);
			set_option(mod, opt, v);
		}
		return;
	}
	case OC_BUTTONS:
	case OC_LIST:
		if (!o->choices)
			return;
		if (dir > 0) {
			next_choice(o->choices, cur, v, sizeof v);
		} else {
			/* the previous one: step forward until the next would be cur */
			char a[NAME_LEN], b[NAME_LEN];

			snprintf(a, sizeof a, "%s", cur);
			for (int guard = 0; guard < 64; guard++) {
				next_choice(o->choices, a, b, sizeof b);
				if (!strcmp(b, cur))
					break;
				snprintf(a, sizeof a, "%s", b);
			}
			snprintf(v, sizeof v, "%s", a);
		}
		set_option(mod, opt, v);
		return;
	}
}

/* a module's options in the order every front end shows them: by section */
static int
option_order(int mod, int *out)
{
	int n = 0, no = themer_option_count(mod);

	for (int g = 0; g < OG_N; g++)
		for (int j = 0; j < no; j++)
			if (front_option_group(mod, j) == g)
				out[n++] = j;
	return n;
}

/* Two panes: modules on the left, the selected module's options on the right, in
 * sections (Server, Look, What it themes) as in the other front ends; a value the
 * user set is marked with *. j/k move within a pane, h/l (or the arrows) switch
 * panes, enter acts: on a module it loads or unloads it, on an option it changes
 * the value (the next choice, a set from the list, or typed). r resets an option to
 * its default; s starts or stops themer-web on a module with a server. */
static void
modules_menu(void)
{
	int order[MAX_MODULES], oo[MAX_OPTS];
	int n = front_module_order(order, MAX_MODULES), sel = 0, osel = 0, focus = 0, k;

	if (!n) {
		flash("no modules found");
		return;
	}
	for (;;) {
		int vis, off, i, no, inner, pane, total, left, top, j, mod = order[sel], lines;
		char b[256];

		getsize();
		no = option_order(mod, oo);
		if (osel >= no)
			osel = no ? no - 1 : 0;
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
		/* the options pane: the module line, a header per section, the server
		 * status, the options, a blank, the description and the range */
		lines = 1 + no + 4;
		for (int g = 0, last = -1; g < no; g++)
			if (front_option_group(mod, oo[g]) != last)
				lines++, last = front_option_group(mod, oo[g]);
		inner = vis > lines ? vis : lines;
		pane = (cols - 6) / 2;
		if (pane > PANEL_W / 2 + 18)
			pane = PANEL_W / 2 + 18;
		if (pane < 22)
			pane = 22;
		total = 2 * pane + 2;
		left = (cols - total) / 2 + 1;
		if (left < 1)
			left = 1;
		top = (rows - (inner + 2)) / 2 + 1;
		if (top < 3)
			top = 3;

		frame_begin(!focus ? "j/k move   enter on/off   l options   q back"
		            : no && front_option_action(cfg, mod, oo[osel])
		            ? "j/k move   h/l change   i install   r reset   esc modules"
		            : "j/k move   h/l change   enter choose or type   r reset   esc modules");

		pfocus = focus ? -1 : 1 + sel - off;
		pbegin_at("modules", inner, pane, left, top);
		for (i = 0; i < vis; i++) {
			int m = order[off + i];

			snprintf(b, sizeof b, "%c [%c] %-12s %s", off + i == sel ? '>' : ' ',
			         themer_module_on(cfg, m) ? 'x' : ' ', themer_module_name(m),
			         themer_module_present(cfg, m) ? "" : front_text(FT_STATE_ABSENT));
			pline(off + i == sel && !focus ? ST_SEL : ST_NORM, b);
		}
		for (; i < inner; i++)
			pline(ST_NORM, "");
		pbottom();

		pfocus = -1;
		if (focus) {
			/* the selected option's line: the status line, its section headers, the rows */
			pfocus = 2 + MOD_HEAD + osel;
			for (int g = 0, last = -1; g <= osel && g < no; g++)
				if (front_option_group(mod, oo[g]) != last)
					pfocus++, last = front_option_group(mod, oo[g]);
		}
		pbegin_at(no ? "options" : "options (none)", inner, pane, left + pane + 2, top);
		snprintf(b, sizeof b, "%s: %s", themer_module_name(mod), front_module_state(cfg, mod));
		pline(ST_DIM, b);
		{
			/* what it themes, and why it does nothing here, as the GUIs say it */
			char about[512];

			snprintf(about, sizeof about, "%s%s%s", themer_module_desc(mod),
			         front_module_note(cfg, mod) ? ". " : "",
			         front_module_note(cfg, mod) ? front_module_note(cfg, mod) : "");
			note_lines(about, MOD_HEAD, pane - 4 > 10 ? pane - 4 : 10);
		}
		i = 1 + MOD_HEAD;
		for (j = 0; j < no; j++) {
			int g = front_option_group(mod, oo[j]);
			const TmOption *o = themer_option(mod, oo[j]);

			if (!j || g != front_option_group(mod, oo[j - 1])) {
				pline(ST_DIM, front_option_groups[g]);
				i++;
			}
			char shown[160];

			option_shown(mod, oo[j], shown, sizeof shown);
			snprintf(b, sizeof b, "  %-12s %s%s", o->name, shown,
			         themer_option_is_set(cfg, mod, oo[j]) ? " *" : "");
			if (front_option_action(cfg, mod, oo[j]))
				snprintf(b + strlen(b), sizeof b - strlen(b), "   i: %s", front_option_action(cfg, mod, oo[j]));
			pline(focus && j == osel ? ST_SEL : ST_NORM, b);
			i++;
		}
		if (!no) {
			pline(ST_DIM, front_text(FT_NO_OPTIONS));
			i++;
		} else {
			const TmOption *o = themer_option(mod, oo[osel]);
			char note[512];

			pline(ST_NORM, "");
			/* the line the other front ends show under the option: its description,
			 * and its default when it is set; then the values it takes */
			front_option_note(cfg, mod, oo[osel], note, sizeof note);
			note_lines(note, 3, pane - 4 > 10 ? pane - 4 : 10);
			option_range(o, b, sizeof b);
			pline(ST_DIM, b);
			i += 5;
		}
		for (; i < inner; i++)
			pline(ST_NORM, "");
		pbottom();
		frame_end();

		k = readkey();
		switch (k) {
		case 'j':
		case K_DOWN:
			if (!focus && sel < n - 1)
				sel++, osel = 0;
			else if (focus && osel < no - 1)
				osel++;
			break;
		case 'k':
		case K_UP:
			if (!focus && sel > 0)
				sel--, osel = 0;
			else if (focus && osel > 0)
				osel--;
			break;
		case 'l':
		case K_RIGHT:
			if (focus)
				option_step(mod, oo[osel], 1);
			else if (no)
				focus = 1;
			else
				flash(front_text(FT_NO_OPTIONS));
			break;
		case 'h':
		case K_LEFT:
			if (focus)
				option_step(mod, oo[osel], -1);
			break;
		case 'i':
			if (focus && no && front_option_action(cfg, mod, oo[osel])) {
				char msg[256];

				front_option_run(cfg, mod, oo[osel], msg, sizeof msg);
				flash(msg);
			}
			break;
		case 'r':
			if (focus && no) {
				char msg[256];

				front_option_reset(cfg, mod, oo[osel], msg, sizeof msg);
				flash(msg);
			}
			break;
		case K_ENTER:
			if (focus) {
				const TmOption *o = themer_option(mod, oo[osel]);
				const char *cur = themer_option_value(cfg, mod, oo[osel]);
				char next[NAME_LEN], buf[NAME_LEN];

				if (o->choices && front_option_control(mod, oo[osel]) != OC_LIST) {
					next_choice(o->choices, cur, next, sizeof next);
					set_option(mod, oo[osel], next);
				} else if (o->choices) {
					/* a long list of choices: picked from a list */
					static char ch[64][NAME_LEN];
					const char *items[64];
					int nc = 0, at = 0, r;

					for (const char *p = o->choices; *p && nc < 64; ) {
						size_t len = strcspn(p, "|");

						snprintf(ch[nc], NAME_LEN, "%.*s", (int)len, p);
						if (!strcmp(ch[nc], cur))
							at = nc;
						items[nc] = ch[nc];
						nc++;
						p += len;
						if (*p)
							p++;
					}
					r = pick(o->name, o->desc, items, nc, &at, "enter pick   q back");
					if (r >= 0)
						set_option(mod, oo[osel], ch[r]);
				} else if (!strcmp(o->name, "template")) {
					static char sets[MAX_NAMES][NAME_LEN], titles[MAX_NAMES][NAME_LEN];
					const char *items[MAX_NAMES];
					int ns = template_all_sets(sets, MAX_NAMES), at = 0, r;

					for (int s = 0; s < ns; s++) {
						items[s] = front_set_title(sets[s], titles[s], sizeof titles[s]);
						if (!strcmp(sets[s], cur))
							at = s;
					}
					r = pick("Template set", o->desc, items, ns, &at, "enter pick   q back");
					if (r >= 0)
						set_option(mod, oo[osel], sets[r]);
				} else {
					snprintf(buf, sizeof buf, "%s", cur);
					if (input(o->name, o->desc, buf, sizeof buf))
						set_option(mod, oo[osel], buf);
				}
			} else {
				int on = themer_module_on(cfg, mod);

				if (front_module_set(cfg, mod, !on) < 0)
					flash(front_text(on ? FT_MODULE_UNLOAD_FAILED : FT_MODULE_LOAD_FAILED));
				else
					flash(front_text(on ? FT_MODULE_UNLOADED : FT_MODULE_LOADED));
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

	path_under_home(path, sizeof path, ".config/themer/cache/report.txt");
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

	if (!input(front_text(FT_SET_NEW_TITLE), front_text(FT_SET_NEW_PROMPT), name, sizeof name) || !name[0])
		return;
	args[0] = from;
	args[1] = name;
	if (with_report("set copy", do_copy, args) < 0) {
		flash(front_text(FT_SET_COPY_FAILED));
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
	if (!input(front_text(FT_SET_IMPORT_SET), front_text(FT_SET_IMPORT_SET_HINT), name, sizeof name) || !name[0])
		return;
	for (int i = 0; i < themer_module_count(); i++)
		if (themer_module_templates(i))
			mods[nm++] = themer_module_name(i);
	r = pick(front_text(FT_SET_IMPORT_MODULE), front_text(FT_SET_IMPORT_NOTE), mods, nm, &sel, "j/k move   enter select   esc back");
	if (r < 0)
		return;
	snprintf(module, sizeof module, "%s", mods[r]);
	if (!input(front_text(FT_SET_IMPORT_PATH), front_text(FT_SET_IMPORT_PATH_HINT), path, sizeof path) || !path[0])
		return;
	args[0] = name;
	args[1] = module;
	args[2] = path;
	if (with_report("set import", do_import, args) < 0) {
		flash(front_text(FT_SET_IMPORT_FAILED));
		return;
	}
	if (front_set_set(cfg, name) == 0)
		dirty = 1;
}

static void
set_pick(void)
{
	static char names[MAX_NAMES][NAME_LEN], titles[MAX_NAMES][NAME_LEN * 2 + 4];
	const char *items[MAX_NAMES];
	int n, sel = 0, r;

	for (;;) {
		n = template_all_sets(names, MAX_NAMES);
		if (!n) {
			flash("no template sets found");
			return;
		}
		/* the title people read, the directory name beside it when it differs */
		for (int i = 0; i < n; i++) {
			char t[NAME_LEN];

			front_set_title(names[i], t, sizeof t);
			if (strcmp(t, names[i]))
				snprintf(titles[i], sizeof titles[i], "%-24s %s", t, names[i]);
			else
				snprintf(titles[i], sizeof titles[i], "%s", t);
			items[i] = titles[i];
			if (!strcmp(names[i], cfg->set))
				sel = i;
		}
		r = pick(front_text(FT_SET), front_text(FT_SET_NOTE),
		         items, n, &sel, "enter select   n new (copy)   i import a theme   c check   esc back");
		if (r == -1)
			return;
		if (r >= 0) {
			if (strcmp(cfg->set, names[r]) && front_set_set(cfg, names[r]) == 0)
				dirty = 1;
			return;
		}
		if (last_key == 'n')
			set_new(names[sel]);
		else if (last_key == 'i')
			set_import_tui(names[sel]);
		else if (last_key == 'c')
			with_report("set check", do_check, (void *)names[sel]);
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
		if (input(front_text(FT_FONT_TTY), front_text(FT_FONT_TTY_TIP), cfg->tty_font, sizeof cfg->tty_font))
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
		r = pick(front_text(FT_RENDERING), front_text(FT_RENDERING_NOTE),
		         items, 3, &sel, "j/k move   enter change   esc back");
		if (r < 0)
			return;
		if (r == 0) {
			int s = 0, k;

			for (int i = 0; i < 4; i++)
				if (!strcmp(hinting_values[i], cfg->hinting))
					s = i;
			k = pick(front_text(FT_HINTING), "slight suits most screens; full is crisp but uneven",
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
			k = pick(front_text(FT_RGBA), "rgb for most LCDs; none for HiDPI or rotated screens",
			         (const char **)rgba_values, 5, &s, "j/k move   enter select   esc back");
			if (k >= 0 && strcmp(cfg->rgba, rgba_values[k]) && front_set_rgba(cfg, rgba_values[k]) == 0)
				dirty = 1;
		}
	}
}

/* Applies everything with stderr sent to ~/.config/themer/cache/errors.log, so the
 * messages do not draw over the screen. The first line is shown in the status. */
static void
apply_all(void)
{
	char dir[PATH_MAX], log[PATH_MAX], first[160] = "", done[256];
	int saved, fd, r;
	FILE *f;

	path_under_home(dir, sizeof dir, ".config/themer/cache");
	mkdir(dir, 0755);
	path_under_home(log, sizeof log, ".config/themer/cache/errors.log");

	fflush(stderr);
	saved = dup(STDERR_FILENO);
	fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0)
		dup2(fd, STDERR_FILENO);
	r = front_apply(cfg, done, sizeof done);
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
		flash(done);
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
/* the Server screen: themer-web, which serves the web front end and the stylus
 * module's userstyles. Started and stopped by hand; the port is saved. */
static void
server_menu(void)
{
	int sel = 0, k;

	for (;;) {
		int port = front_web_port(cfg), up = front_web_running(port);
		char run[128], portl[64], url[96], open[96];
		const char *items[3];
		int nrow = up ? 3 : 2;

		snprintf(run, sizeof run, "  %s", front_text(up ? FT_SERVER_STOP : FT_SERVER_START));
		snprintf(portl, sizeof portl, "  %-15s%d", front_text(FT_PORT), port);
		snprintf(url, sizeof url, "http://localhost:%d/", port);
		items[0] = run;
		items[1] = portl;
		if (sel >= nrow)
			sel = nrow - 1;
		snprintf(open, sizeof open, "  %s", front_text(FT_SERVER_OPEN));
		items[2] = open;
		frame_begin("j/k move   enter select   q back");
		pbegin("server", 11, PANEL_W);
		pline(ST_DIM, front_text(up ? FT_SERVER_UP : FT_SERVER_DOWN));
		pline(ST_NORM, up ? url : "");
		pline(ST_NORM, "");
		for (int i = 0; i < nrow; i++)
			pline(i == sel ? ST_SEL : ST_NORM, items[i]);
		pline(ST_NORM, "");
		note_lines(front_text(FT_SERVER_NOTE), 3, PANEL_W - 4 > cols - 10 ? cols - 10 : PANEL_W - 4);
		pbottom();
		frame_end();
		k = readkey();
		if ((k == 'j' || k == K_DOWN) && sel < nrow - 1)
			sel++;
		else if ((k == 'k' || k == K_UP) && sel > 0)
			sel--;
		else if (k == 'q' || k == K_ESC)
			return;
		else if (k == K_ENTER && sel == 0) {
			if ((up ? front_web_stop(port) : front_web_start(port)) < 0)
				flash(front_text(up ? FT_SERVER_STOP_FAILED : FT_SERVER_START_FAILED));
			else
				flash(front_text(up ? FT_SERVER_STOPPED : FT_SERVER_STARTED));
		} else if (k == K_ENTER && sel == 1) {
			char buf[16];

			snprintf(buf, sizeof buf, "%d", port);
			if (input(front_text(FT_PORT), front_text(FT_PORT_NOTE), buf, sizeof buf) &&
			    front_set_web_port(cfg, atoi(buf)) < 0)
				flash("the port is a number from 1024 to 65535");
		} else if (k == K_ENTER && sel == 2) {
			flash(front_open(url) == 0 ? "opened in the browser" : "xdg-open could not open it");
		}
	}
}


/* The main menu: one row per setting, under the pages of the graphical front ends
 * (front_pages) and with their words (front_text), so the four front ends read the
 * same; the line under the selected row's section in the GUIs is shown under the
 * list. The actions close it, as the File menu does. */
enum {
	R_THEME, R_MODE, R_SET, R_COLORS,
	R_UIFONT, R_UISIZE, R_MONOFONT, R_MONOSIZE, R_TTYFONT, R_RENDER,
	R_ICONS, R_CURSOR, R_CURSIZE,
	R_MODULES,
	R_SERVER, R_ENV, R_ABOUT,
	R_APPLY, R_REVERT, R_QUIT, R_N
};

static const struct { int page, label, note; } menu_rows[R_N] = {
	[R_THEME]    = { FP_THEME,   FT_SCHEME,           -1 },
	[R_MODE]     = { -1,         FT_MODE,             -1 },
	[R_SET]      = { -1,         FT_SET,              FT_SET_NOTE },
	[R_COLORS]   = { -1,         FT_COLORS,           FT_COLORS_NOTE },
	[R_UIFONT]   = { FP_FONTS,   FT_FONT_UI,          FT_FONTS_NOTE },
	[R_UISIZE]   = { -1,         FT_ROW_UI_SIZE,      FT_FONTS_NOTE },
	[R_MONOFONT] = { -1,         FT_FONT_MONO,        FT_FONTS_NOTE },
	[R_MONOSIZE] = { -1,         FT_ROW_MONO_SIZE,    FT_FONTS_NOTE },
	[R_TTYFONT]  = { -1,         FT_FONT_TTY,         FT_FONTS_NOTE },
	[R_RENDER]   = { -1,         FT_RENDERING,        FT_RENDERING_NOTE },
	[R_ICONS]    = { FP_ICONS,   FT_ICONS,            FT_ICONS_NOTE },
	[R_CURSOR]   = { -1,         FT_CURSOR,           FT_CURSOR_NOTE },
	[R_CURSIZE]  = { -1,         FT_ROW_CURSOR_SIZE,  FT_CURSOR_NOTE },
	[R_MODULES]  = { FP_MODULES, FT_MODULES,          FT_MODULES_NOTE },
	[R_SERVER]   = { FP_SYSTEM,  FT_SERVER,           FT_SERVER_NOTE },
	[R_ENV]      = { -1,         FT_ENV,              FT_ENV_NOTE },
	[R_ABOUT]    = { -1,         FT_ABOUT,            FT_ABOUT_NOTE },
	[R_APPLY]    = { -2,         FT_APPLY,            -1 },
	[R_REVERT]   = { -1,         FT_REVERT,           -1 },
	[R_QUIT]     = { -1,         FT_QUIT,             -1 },
};
#define NOTE_LINES 3   /* the selected row's note, wrapped */

/* back to what was last applied, as Revert to applied in the other front ends */
static void
revert_all(void)
{
	*cfg = base;
	dirty = 0;
	flash(front_text(FT_REVERTED));
}

/* a size row: left/right steps it, enter types it */
static void
step_size(int row, int d)
{
	int mono = row == R_MONOSIZE;
	int sz = row == R_CURSIZE ? cfg->cursor_size : mono ? cfg->mono_size : cfg->ui_size;

	sz += d;
	if (row == R_CURSIZE) {
		if (sz >= 8 && sz <= 512 && front_set_cursor(cfg, NULL, sz) == 0)
			dirty = 1;
	} else if (sz >= 4 && sz <= 72 && front_set_font(cfg, mono, mono ? cfg->mono_font : cfg->ui_font, sz) == 0) {
		dirty = 1;
	}
}

static void
type_size(int row)
{
	char buf[16];
	int mono = row == R_MONOSIZE, cur = row == R_CURSIZE ? cfg->cursor_size : mono ? cfg->mono_size : cfg->ui_size;

	snprintf(buf, sizeof buf, "%d", cur);
	if (!input(front_text(menu_rows[row].label), row == R_CURSIZE ? "pixels (24, 32, 48...)" : "points", buf, sizeof buf))
		return;
	step_size(row, atoi(buf) - cur);
}

/* the family only; the size is its own row */
static void
font_family_pick(int mono)
{
	char f[NAME_LEN];

	snprintf(f, sizeof f, "%s", mono ? cfg->mono_font : cfg->ui_font);
	if (load_fonts(mono) ? !browse(mono ? "Monospace fonts" : "Installed fonts", f, sizeof f, 1)
	                     : !input("Font family", "no fonts found; type a family name", f, sizeof f))
		return;
	if (front_set_font(cfg, mono, f, 0) == 0)
		dirty = 1;
	else
		flash(front_text(FT_FONT_INVALID));
}

static int
main_action(int row)
{
	switch (row) {
	case R_THEME:    theme_menu(); break;
	case R_MODE:     toggle_mode(); break;
	case R_SET:      set_pick(); break;
	case R_COLORS:   colors_menu(); break;
	case R_UIFONT:   font_family_pick(0); break;
	case R_MONOFONT: font_family_pick(1); break;
	case R_UISIZE: case R_MONOSIZE: case R_CURSIZE: type_size(row); break;
	case R_TTYFONT:  tty_pick(); break;
	case R_RENDER:   render_menu(); break;
	case R_ICONS:    icon_pick(0); break;
	case R_CURSOR:   icon_pick(1); break;
	case R_MODULES:  modules_menu(); break;
	case R_SERVER:   server_menu(); break;
	case R_ENV:      env_menu(); break;
	case R_ABOUT:    break;
	case R_APPLY:    apply_all(); break;
	case R_REVERT:   revert_all(); break;
	default:         return 0;
	}
	return 1;
}

static void
main_menu(void)
{
	int sel = 0, armed = 0, k, nsec = 0;

	for (int r = 0; r < R_N; r++)
		nsec += menu_rows[r].page >= 0 ? 2 : menu_rows[r].page == -2;
	for (;;) {
		char val[R_N][128], line[200], title[NAME_LEN];
		int port = front_web_port(cfg);

		snprintf(val[R_THEME], sizeof val[0], "%s", cfg->th.name);
		snprintf(val[R_MODE], sizeof val[0], "%s", cfg->mode == MODE_DARK ? "< dark >" : "< light >");
		snprintf(val[R_SET], sizeof val[0], "%s", front_set_title(cfg->set, title, sizeof title));
		snprintf(val[R_COLORS], sizeof val[0], "%d", count_overrides());
		snprintf(val[R_UIFONT], sizeof val[0], "%s", cfg->ui_font);
		snprintf(val[R_UISIZE], sizeof val[0], "< %d >", cfg->ui_size);
		snprintf(val[R_MONOFONT], sizeof val[0], "%s", cfg->mono_font);
		snprintf(val[R_MONOSIZE], sizeof val[0], "< %d >", cfg->mono_size);
		snprintf(val[R_TTYFONT], sizeof val[0], "%s", cfg->tty_font);
		snprintf(val[R_RENDER], sizeof val[0], "%s, %s, %s", cfg->hinting,
		         cfg->antialias ? "antialiased" : "aliased", cfg->rgba);
		snprintf(val[R_ICONS], sizeof val[0], "%s", cfg->icons);
		snprintf(val[R_CURSOR], sizeof val[0], "%s", cfg->cursor);
		snprintf(val[R_CURSIZE], sizeof val[0], "< %d >", cfg->cursor_size);
		snprintf(val[R_MODULES], sizeof val[0], "%d of %d on", modules_on(), themer_module_count());
		if (front_web_running(port))
			snprintf(val[R_SERVER], sizeof val[0], "running, localhost:%d", port);
		else
			snprintf(val[R_SERVER], sizeof val[0], "stopped");
		snprintf(val[R_ENV], sizeof val[0], "%d variable%s", cfg->nenv, cfg->nenv == 1 ? "" : "s");
		snprintf(val[R_ABOUT], sizeof val[0], "themer %s", VERSION);
		snprintf(val[R_APPLY], sizeof val[0], "%s", front_text(dirty ? FT_UNAPPLIED : FT_UP_TO_DATE));
		val[R_REVERT][0] = val[R_QUIT][0] = '\0';

		frame_begin("j/k move   enter change   h/l adjust   m mode   a apply   r revert   q quit");
		/* the selected row's line in the panel, kept in view in a short terminal */
		pfocus = 1;
		for (int r = 0; r <= sel; r++)
			pfocus += (menu_rows[r].page >= 0 ? (r ? 2 : 1) : menu_rows[r].page == -2) + (r < sel);
		pbegin("themer", R_N + nsec + 4 + NOTE_LINES, PANEL_W);
		for (int r = 0; r < R_N; r++) {
			if (menu_rows[r].page != -1) {
				if (r)
					pline(ST_NORM, "");
				if (menu_rows[r].page >= 0)
					pline(ST_DIM, front_pages[menu_rows[r].page].title);
			}
			snprintf(line, sizeof line, "  %-17s%.120s", front_text(menu_rows[r].label), val[r]);
			pline(r == sel ? ST_SEL : ST_NORM, line);
		}
		pline(ST_NORM, "");
		note_lines(menu_rows[sel].note >= 0 ? front_text(menu_rows[sel].note) : "", NOTE_LINES,
		           PANEL_W - 4 > cols - 10 ? cols - 10 : PANEL_W - 4);
		pline(ST_NORM, "");
		preview_lines();
		pbottom();
		frame_end();

		k = readkey();
		if (k == K_ENTER && sel == R_QUIT)
			k = 'q';       /* the Quit row asks as q does */
		if (k != 'q' && k != K_ESC)
			armed = 0;
		switch (k) {
		case 'j':
		case K_DOWN:
			if (sel < R_N - 1)
				sel++;
			break;
		case 'k':
		case K_UP:
			if (sel > 0)
				sel--;
			break;
		case 'h':
		case 'l':
		case K_LEFT:
		case K_RIGHT:
			if (sel == R_MODE)
				toggle_mode();
			else if (sel == R_UISIZE || sel == R_MONOSIZE || sel == R_CURSIZE)
				step_size(sel, k == 'l' || k == K_RIGHT ? (sel == R_CURSIZE ? 8 : 1)
				                                        : (sel == R_CURSIZE ? -8 : -1));
			break;
		case 'm':
			toggle_mode();
			break;
		case 'a':
			apply_all();
			break;
		case 'r':
			revert_all();
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
	base = *c;
	front_track(cfg, &base);
	term_on();
	main_menu();
	term_off();
}
