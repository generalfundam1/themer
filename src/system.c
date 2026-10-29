/* system.c: the root side of the console palette and font, and ly's colors.
 *
 *   sudo themer system install [USER]   once: a service that keeps them in step
 *   sudo themer system remove [USER]    everything back as it was, the service gone
 *   themer system sync USER             (root) put USER's files into effect once
 *   themer system watch USER            (root) sync, then again on every change
 *   themer system status [USER]
 *
 * Every apply writes, as the user, what these should be into
 * ~/.config/themer/system/ (see targets.c and modules/ly.c). Install sets up, for the
 * init system running, what puts them into effect as root, at boot and after every
 * change: with systemd a path unit watching that directory that runs the sync; with
 * OpenRC, runit, s6 (s6-rc), dinit or SysV init a service running the watch, which
 * follows the directory with inotify. With none of these, install syncs once and
 * says what to run.
 *
 * The sync runs as root on files a user wrote, so it trusts nothing in them:
 *  - they are read by a child that has become the user, never following a link,
 *    only regular files the user owns, at most a few kilobytes;
 *  - every value is checked against what it may be (sixteen palette lines of
 *    decimals, a font name, ly's color keys with 0x values) and anything else is
 *    refused, so no text of the user's reaches a file that root runs as code
 *    (ly's config.lua is Lua);
 *  - one user at a time: /etc/themer/user names who it was installed for.
 *
 * What it changes in /etc it changes key by key and undoes key by key: for each key
 * /etc/themer/backup/ records the line that was there (or that there was none) and the
 * line themer wrote. Putting back restores only the lines that still read as themer
 * wrote them, so whatever else the admin changed in the meantime stays.
 */
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "themer.h"

#define ETC_DIR    "/etc/themer"
#define BACKUP_DIR ETC_DIR "/backup"
#define USER_FILE  ETC_DIR "/user"
#define USER_MAX   4096                 /* the largest of the user's files read */
#define MAX_KEYS   16

typedef struct {
	char name[33], home[PATH_MAX - 128];
	uid_t uid;
	gid_t gid;
} Who;

/* ---- small things ---- */

static int
exists(const char *p)
{
	struct stat st;

	return lstat(p, &st) == 0;
}

/* a user name as useradd allows it, so it is safe in unit names and paths */
static int
valid_user(const char *u)
{
	size_t n = strlen(u);

	if (!n || n > 32 || u[0] == '-' || u[0] == '.')
		return 0;
	for (const char *p = u; *p; p++)
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
		      *p == '_' || *p == '-' || *p == '.'))
			return 0;
	return 1;
}

static int
who(const char *user, Who *w)
{
	struct passwd *pw;

	if (!valid_user(user) || !(pw = getpwnam(user)) || !pw->pw_dir || pw->pw_dir[0] != '/')
		return -1;
	snprintf(w->name, sizeof w->name, "%s", user);
	if (snprintf(w->home, sizeof w->home, "%s", pw->pw_dir) >= (int)sizeof w->home)
		return -1;
	w->uid = pw->pw_uid;
	w->gid = pw->pw_gid;
	return 0;
}

/* runs a program (a full path, or found in the usual places: a service's PATH is
 * short), without a shell, its output dropped */
static int
spawn(const char *prog, const char *a1, const char *a2, const char *a3, const char *a4)
{
	static const char *const dirs[] = { "/usr/bin", "/usr/sbin", "/bin", "/sbin", "/usr/local/bin", "/usr/local/sbin" };
	char p[PATH_MAX];
	const char *argv[6] = { prog, a1, a2, a3, a4, NULL };
	pid_t pid;
	int st;
	size_t i;

	if (prog[0] == '/') {
		snprintf(p, sizeof p, "%s", prog);
		if (access(p, X_OK))
			return -1;
	} else {
		for (i = 0; i < TM_COUNT(dirs); i++)
			if (snprintf(p, sizeof p, "%s/%s", dirs[i], prog) < (int)sizeof p && !access(p, X_OK))
				break;
		if (i == TM_COUNT(dirs))
			return -1;
	}
	if ((pid = fork()) < 0)
		return -1;
	if (!pid) {
		int nul = open("/dev/null", O_RDWR);

		if (nul >= 0) {
			dup2(nul, 0);
			dup2(nul, 1);
			dup2(nul, 2);
		}
		execv(p, (char *const *)argv);
		_exit(127);
	}
	while (waitpid(pid, &st, 0) < 0)
		if (errno != EINTR)
			return -1;
	return WIFEXITED(st) && !WEXITSTATUS(st) ? 0 : -1;
}

/* ---- reading the user's files, as the user ---- */

enum { UR_ABSENT = -1, UR_BAD = -2 };

static int
become(const Who *w)
{
	return setgroups(0, NULL) < 0 || setgid(w->gid) < 0 || setuid(w->uid) < 0 || getuid() != w->uid ? -1 : 0;
}

/* the contents of one of the user's files: its length, UR_ABSENT when it is not
 * there, UR_BAD when it is not a plain file of theirs, too large or unreadable.
 * With buf NULL, only whether the directory path is there (1) or not (UR_ABSENT). */
static int
user_read(const Who *w, const char *path, char *buf, size_t cap)
{
	int pfd[2], st;
	pid_t pid;
	ssize_t r;
	size_t n = 0;

	if (pipe(pfd) < 0)
		return UR_BAD;
	if ((pid = fork()) < 0) {
		close(pfd[0]);
		close(pfd[1]);
		return UR_BAD;
	}
	if (!pid) {
		struct stat sb;
		char b[USER_MAX];
		int fd;
		ssize_t got;

		close(pfd[0]);
		if (become(w) < 0)
			_exit(2);
		if (!buf) {
			if (stat(path, &sb) < 0)
				_exit(errno == ENOENT || errno == ENOTDIR ? 3 : 2);
			_exit(S_ISDIR(sb.st_mode) ? 0 : 3);
		}
		if ((fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)) < 0)
			_exit(errno == ENOENT || errno == ENOTDIR ? 3 : 2);
		if (fstat(fd, &sb) < 0 || !S_ISREG(sb.st_mode) || sb.st_uid != w->uid || sb.st_size >= (off_t)sizeof b)
			_exit(2);
		got = read(fd, b, sizeof b);
		if (got < 0 || (size_t)got >= sizeof b || write(pfd[1], b, (size_t)got) != got)
			_exit(2);
		_exit(0);
	}
	close(pfd[1]);
	while (buf && (r = read(pfd[0], buf + n, cap - 1 - n)) != 0) {
		if (r < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if ((n += (size_t)r) >= cap - 1)
			break;
	}
	close(pfd[0]);
	while (waitpid(pid, &st, 0) < 0)
		if (errno != EINTR)
			return UR_BAD;
	if (!WIFEXITED(st))
		return UR_BAD;
	if (WEXITSTATUS(st) == 3)
		return UR_ABSENT;
	if (WEXITSTATUS(st))
		return UR_BAD;
	if (!buf)
		return 1;
	buf[n] = '\0';
	return memchr(buf, '\0', n) ? UR_BAD : (int)n;
}

/* makes the user's system directory, as the user */
static int
user_mkdir(const Who *w, const char *dir)
{
	pid_t pid = fork();
	int st;

	if (pid < 0)
		return -1;
	if (!pid)
		_exit(become(w) < 0 || mkdirs(dir) < 0 ? 1 : 0);
	while (waitpid(pid, &st, 0) < 0)
		if (errno != EINTR)
			return -1;
	return WIFEXITED(st) && !WEXITSTATUS(st) ? 0 : -1;
}

/* KEY=VALUE from the user's text, quotes taken off; -1 when it is not there */
static int
text_value(const char *text, const char *key, char *out, size_t n)
{
	size_t kl = strlen(key);
	int found = -1;

	for (const char *l = text; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
		const char *e = strchr(l, '\n');
		size_t len = e ? (size_t)(e - l) : strlen(l);

		if (len > kl && !strncmp(l, key, kl) && l[kl] == '=') {
			const char *v = l + kl + 1;
			size_t vl = len - kl - 1;

			if (vl >= 2 && v[0] == '"' && v[vl - 1] == '"') {
				v++;
				vl -= 2;
			}
			if (vl >= n)
				return -2;
			memcpy(out, v, vl);
			out[vl] = '\0';
			found = 0;
		}
	}
	return found;
}

/* ---- what the values may be ---- */

/* setvtrgb's format: three lines (red, green, blue) of sixteen decimals 0-255,
 * written back out clean */
static int
parse_palette(const char *t, char *out, size_t n)
{
	size_t o = 0;

	for (int ch = 0; ch < 3; ch++) {
		for (int i = 0; i < 16; i++) {
			char *e;
			long v = strtol(t, &e, 10);

			if (e == t || v < 0 || v > 255 || *e != (i < 15 ? ',' : '\n'))
				return -1;
			o += (size_t)snprintf(out + o, n - o, "%ld%c", v, i < 15 ? ',' : '\n');
			if (o >= n)
				return -1;
			t = e + 1;
		}
	}
	return *t ? -1 : 0;
}

static int
valid_font(const char *v)
{
	if (!*v || strlen(v) > 64 || v[0] == '-' || v[0] == '.')
		return 0;
	for (; *v; v++)
		if (!((*v >= 'a' && *v <= 'z') || (*v >= 'A' && *v <= 'Z') || (*v >= '0' && *v <= '9') ||
		      *v == '_' || *v == '-' || *v == '.'))
			return 0;
	return 1;
}

static const char *const ly_keys[] = { "bg", "fg", "border_fg", "error_fg", "error_bg", "cmatrix_fg",
	"gameoflife_fg", "colormix_col1", "colormix_col2", "colormix_col3" };

/* 0x and eight hex digits */
static int
valid_ly_color(const char *v)
{
	if (strncmp(v, "0x", 2) || strlen(v) != 10)
		return 0;
	for (v += 2; *v; v++)
		if (!((*v >= '0' && *v <= '9') || (*v >= 'A' && *v <= 'F') || (*v >= 'a' && *v <= 'f')))
			return 0;
	return 1;
}

/* every line of ly.conf a comment, blank, or one of ly's color keys: anything else
 * and the whole file is refused */
static int
ly_lines_ok(const char *t)
{
	for (const char *l = t; *l; ) {
		const char *e = strchr(l, '\n');
		size_t len = e ? (size_t)(e - l) : strlen(l), k;

		if (len && l[0] != '#') {
			for (k = 0; k < TM_COUNT(ly_keys); k++) {
				size_t kl = strlen(ly_keys[k]);

				if (len > kl && !strncmp(l, ly_keys[k], kl) && l[kl] == '=')
					break;
			}
			if (k == TM_COUNT(ly_keys))
				return 0;
		}
		l += len + (e ? 1 : 0);
	}
	return 1;
}

/* ---- editing a file in /etc, key by key ---- */

typedef enum { ST_SH, ST_INI, ST_LUA } Style;

typedef struct {
	char key[32];
	char was[512];      /* the line before themer, when has_was */
	char set[512];      /* the line themer wrote */
	int has_was;
} Rec;

typedef struct {
	Rec r[MAX_KEYS];
	int n, file_absent;
} Recs;

typedef struct {
	char *s;
	size_t n, cap;
} Buf;

static int
buf_add(Buf *b, const char *s, size_t n)
{
	if (b->n + n + 1 > b->cap) {
		size_t cap = (b->n + n + 1) * 2;
		char *t = realloc(b->s, cap);

		if (!t)
			return -1;
		b->s = t;
		b->cap = cap;
	}
	memcpy(b->s + b->n, s, n);
	b->n += n;
	b->s[b->n] = '\0';
	return 0;
}

/* a file of root's in /etc, at most a megabyte; NULL with errno ENOENT when absent */
static char *
etc_read(const char *path)
{
	FILE *f = fopen(path, "rb");
	Buf b = { 0 };
	char chunk[4096];
	size_t r;

	if (!f)
		return NULL;
	while ((r = fread(chunk, 1, sizeof chunk, f)) > 0)
		if (b.n + r > (1 << 20) || buf_add(&b, chunk, r) < 0) {
			free(b.s);
			fclose(f);
			errno = EFBIG;
			return NULL;
		}
	if (ferror(f)) {
		free(b.s);
		fclose(f);
		errno = EIO;
		return NULL;
	}
	fclose(f);
	return b.s ? b.s : calloc(1, 1);
}

/* writes a file in /etc whole and safely: a link is followed to the file it names,
 * the owner and mode are kept, the new file is on disk before it takes the old one's
 * place, and SELinux gets its label back */
static int
etc_write(const char *path, const char *text, mode_t mode_new)
{
	char real[PATH_MAX], tmp[PATH_MAX + 32], dir[PATH_MAX], *slash;
	struct stat st;
	int fd, have = 0;
	size_t len = strlen(text);

	if (realpath(path, real)) {
		have = stat(real, &st) == 0;
	} else if (errno == ENOENT) {
		snprintf(real, sizeof real, "%s", path);
	} else {
		return -1;
	}
	snprintf(dir, sizeof dir, "%s", real);
	if (!(slash = strrchr(dir, '/')))
		return -1;
	*slash = '\0';
	if (snprintf(tmp, sizeof tmp, "%s/.%s.themer-XXXXXX", dir[0] ? dir : "/", slash + 1) >= (int)sizeof tmp)
		return -1;
	if ((fd = mkstemp(tmp)) < 0)
		return -1;
	if (fchmod(fd, have ? st.st_mode & 07777 : mode_new) < 0 ||
	    (have && fchown(fd, st.st_uid, st.st_gid) < 0) ||
	    write(fd, text, len) != (ssize_t)len || fsync(fd) < 0) {
		close(fd);
		unlink(tmp);
		return -1;
	}
	if (close(fd) < 0 || rename(tmp, real) < 0) {
		unlink(tmp);
		return -1;
	}
	if ((fd = open(dir[0] ? dir : "/", O_RDONLY | O_DIRECTORY)) >= 0) {
		fsync(fd);
		close(fd);
	}
	spawn("restorecon", real, NULL, NULL, NULL);
	return 0;
}

static void
rec_name(const char *path, char *out, size_t n)
{
	char flat[PATH_MAX - 64];

	snprintf(flat, sizeof flat, "%s", path);
	for (char *p = flat; *p; p++)
		if (*p == '/')
			*p = '_';
	snprintf(out, n, "%s/%s", BACKUP_DIR, flat);
}

/* the record of what themer changed in a file: 0, or -1 when there is none */
static int
rec_load(const char *path, Recs *rs)
{
	char name[PATH_MAX], *t, *l, *save = NULL;
	Rec *cur = NULL;

	memset(rs, 0, sizeof *rs);
	rec_name(path, name, sizeof name);
	if (!(t = etc_read(name)))
		return -1;
	for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
		if (!strcmp(l, "file absent")) {
			rs->file_absent = 1;
		} else if (!strncmp(l, "key ", 4) && rs->n < MAX_KEYS) {
			cur = &rs->r[rs->n++];
			snprintf(cur->key, sizeof cur->key, "%s", l + 4);
		} else if (cur && !strncmp(l, "was ", 4)) {
			snprintf(cur->was, sizeof cur->was, "%s", l + 4);
			cur->has_was = 1;
		} else if (cur && !strncmp(l, "set ", 4)) {
			snprintf(cur->set, sizeof cur->set, "%s", l + 4);
		}
	}
	free(t);
	return 0;
}

static int
rec_save(const char *path, const Recs *rs)
{
	char name[PATH_MAX];
	Buf b = { 0 };
	int rc;

	rec_name(path, name, sizeof name);
	if (!rs->n && !rs->file_absent)
		return unlink(name) < 0 && errno != ENOENT ? -1 : 0;
	if (mkdirs(BACKUP_DIR) < 0)
		return -1;
	chmod(ETC_DIR, 0755);
	chmod(BACKUP_DIR, 0700);
	buf_add(&b, "# themer system: the lines it changed here, to put back\n", 56);
	if (rs->file_absent)
		buf_add(&b, "file absent\n", 12);
	for (int i = 0; i < rs->n; i++) {
		const Rec *r = &rs->r[i];

		buf_add(&b, "key ", 4);
		buf_add(&b, r->key, strlen(r->key));
		buf_add(&b, "\n", 1);
		if (r->has_was) {
			buf_add(&b, "was ", 4);
			buf_add(&b, r->was, strlen(r->was));
			buf_add(&b, "\n", 1);
		}
		buf_add(&b, "set ", 4);
		buf_add(&b, r->set, strlen(r->set));
		buf_add(&b, "\n", 1);
	}
	rc = b.s ? etc_write(name, b.s, 0600) : -1;
	free(b.s);
	return rc;
}

/* does this line set key? In Lua only inside the ly = { ... } table */
static int
line_is_key(const char *l, size_t len, const char *key, Style st)
{
	size_t kl = strlen(key), i = 0;

	if (st != ST_SH)
		while (i < len && (l[i] == ' ' || l[i] == '\t'))
			i++;
	if (len - i <= kl || strncmp(l + i, key, kl))
		return 0;
	i += kl;
	if (st != ST_SH)
		while (i < len && (l[i] == ' ' || l[i] == '\t'))
			i++;
	return i < len && l[i] == '=';
}

/* the lines of a file, for editing */
typedef struct {
	const char *s[8192];
	size_t len[8192];
	int n, lua_open, lua_close;   /* the ly table's first and closing lines, in Lua */
} Lines;

static int
split_lines(const char *t, Lines *L, Style st)
{
	L->n = 0;
	L->lua_open = L->lua_close = -1;
	for (const char *s = t; *s; ) {
		const char *e = strchr(s, '\n');
		size_t len = e ? (size_t)(e - s) : strlen(s);

		if (L->n == (int)TM_COUNT(L->s))
			return -1;
		L->s[L->n] = s;
		L->len[L->n] = len;
		if (st == ST_LUA) {
			if (L->lua_open < 0 && len >= 2 && !strncmp(s, "ly", 2)) {
				const char *p = s + 2;

				while (*p == ' ')
					p++;
				if (*p == '=' && strchr(p, '{') && strchr(p, '{') < s + len)
					L->lua_open = L->n;
			} else if (L->lua_open >= 0 && L->lua_close < 0 && len && s[0] == '}') {
				L->lua_close = L->n;
			}
		}
		L->n++;
		s += len + (e ? 1 : 0);
	}
	return st == ST_LUA && (L->lua_open < 0 || L->lua_close < 0) ? -1 : 0;
}

/* the last line that sets key, or -1 */
static int
find_key(const Lines *L, const char *key, Style st)
{
	int from = st == ST_LUA ? L->lua_open + 1 : 0, to = st == ST_LUA ? L->lua_close : L->n;

	for (int i = to - 1; i >= from; i--)
		if (line_is_key(L->s[i], L->len[i], key, st))
			return i;
	return -1;
}

static void
key_line(char *out, size_t n, Style st, const char *key, const char *val)
{
	snprintf(out, n, st == ST_SH ? "%s=%s" : st == ST_INI ? "%s = %s" : "    %s = %s,", key, val);
}

typedef struct {
	const char *key;
	char val[96];
} Want;

/* rebuilds the file's text with line i replaced (or dropped when repl is NULL), and
 * the adds put at the end (or before the ly table's close) */
static char *
rebuild(const Lines *L, const char *const *repl, const char *adds, Style st, int trailing_nl)
{
	Buf b = { 0 };
	int at = st == ST_LUA ? L->lua_close : L->n;

	for (int i = 0; i <= L->n; i++) {
		if (i == at && adds && *adds)
			buf_add(&b, adds, strlen(adds));
		if (i == L->n)
			break;
		if (repl[i] == (const char *)1)
			continue;       /* dropped */
		if (repl[i])
			buf_add(&b, repl[i], strlen(repl[i]));
		else
			buf_add(&b, L->s[i], L->len[i]);
		if (i < L->n - 1 || trailing_nl || (i == at - 1 && adds && *adds))
			buf_add(&b, "\n", 1);
	}
	return b.s ? b.s : calloc(1, 1);
}

/* sets the wanted keys in path and puts back the recorded ones no longer wanted. The
 * record is written before the file, so a failure leaves nothing unrecorded. 1 when
 * the file changed, 0 when it did not, -1 on failure (the file untouched). */
static int
etc_apply(const char *path, Style st, const Want *want, int nwant)
{
	static Lines L;
	static const char *repl[TM_COUNT(L.s)];
	static Rec gone[MAX_KEYS];
	char lines[MAX_KEYS][512], adds[MAX_KEYS * 512 + 1] = "", *t, *out;
	Recs rs;
	int absent = 0, rc, ngone = 0;

	if (!(t = etc_read(path))) {
		if (errno != ENOENT || st != ST_SH)
			return -1;
		absent = 1;
		t = calloc(1, 1);
	}
	if (split_lines(t, &L, st) < 0) {
		fprintf(stderr, "themer: %s: not a file themer can change safely, left as it is\n", path);
		free(t);
		return -1;
	}
	if (rec_load(path, &rs) < 0)
		rs.file_absent = absent;
	memset(repl, 0, sizeof repl);
	for (int k = 0; k < nwant; k++) {
		int i = find_key(&L, want[k].key, st), r;

		key_line(lines[k], sizeof lines[k], st, want[k].key, want[k].val);
		for (r = 0; r < rs.n && strcmp(rs.r[r].key, want[k].key); r++)
			;
		if (r == rs.n) {                /* first time: what was there */
			if (rs.n == MAX_KEYS)
				continue;
			memset(&rs.r[r], 0, sizeof rs.r[r]);
			snprintf(rs.r[r].key, sizeof rs.r[r].key, "%s", want[k].key);
			if (i >= 0 && L.len[i] < sizeof rs.r[r].was) {
				memcpy(rs.r[r].was, L.s[i], L.len[i]);
				rs.r[r].was[L.len[i]] = '\0';
				rs.r[r].has_was = 1;
			}
			rs.n++;
		}
		snprintf(rs.r[r].set, sizeof rs.r[r].set, "%s", lines[k]);
		if (i >= 0) {
			repl[i] = lines[k];
		} else {
			strcat(adds, lines[k]);
			strcat(adds, "\n");
		}
	}
	/* keys recorded but no longer wanted: their own lines back */
	for (int r = 0; r < rs.n; ) {
		int k, i;

		for (k = 0; k < nwant && strcmp(want[k].key, rs.r[r].key); k++)
			;
		if (k < nwant) {
			r++;
			continue;
		}
		gone[ngone] = rs.r[r];          /* kept apart: repl points into it */
		i = find_key(&L, gone[ngone].key, st);
		if (i >= 0 && L.len[i] == strlen(gone[ngone].set) && !strncmp(L.s[i], gone[ngone].set, L.len[i]))
			repl[i] = gone[ngone].has_was ? gone[ngone].was : (const char *)1;
		ngone++;
		rs.r[r] = rs.r[--rs.n];
	}
	out = rebuild(&L, repl, adds, st, !*t || t[strlen(t) - 1] == '\n');
	if (!strcmp(out, t)) {
		rc = rec_save(path, &rs) < 0 ? -1 : 0;
	} else if (rec_save(path, &rs) < 0) {
		fprintf(stderr, "themer: could not record what it changes in %s: left as it is\n", path);
		rc = -1;
	} else {
		rc = etc_write(path, out, 0644) < 0 ? -1 : 1;
	}
	free(out);
	free(t);
	return rc;
}

/* puts back what themer changed in path, the lines that still read as it wrote them;
 * a file that was not there before and holds nothing else goes. 1 when the file
 * changed. The record goes only once the file is written. */
static int
etc_restore(const char *path, Style st)
{
	static Lines L;
	static const char *repl[TM_COUNT(L.s)];
	Recs rs;
	char *t, *out;
	int rc = 0;

	if (rec_load(path, &rs) < 0)
		return 0;
	if (!(t = etc_read(path))) {
		if (errno != ENOENT)
			return -1;
		rs.n = rs.file_absent = 0;      /* gone already: nothing to put back */
		return rec_save(path, &rs) < 0 ? -1 : 0;
	}
	if (split_lines(t, &L, st) < 0) {
		free(t);
		return -1;
	}
	memset(repl, 0, sizeof repl);
	for (int r = 0; r < rs.n; r++) {
		int i = find_key(&L, rs.r[r].key, st);

		if (i >= 0 && L.len[i] == strlen(rs.r[r].set) && !strncmp(L.s[i], rs.r[r].set, L.len[i]))
			repl[i] = rs.r[r].has_was ? rs.r[r].was : (const char *)1;
	}
	out = rebuild(&L, repl, NULL, st, !*t || t[strlen(t) - 1] == '\n');
	if (rs.file_absent && !out[strspn(out, " \t\n")]) {
		if (unlink(path) < 0 && errno != ENOENT)
			rc = -1;
		else
			rc = 1;
	} else if (strcmp(out, t)) {
		rc = etc_write(path, out, 0644) < 0 ? -1 : 1;
	}
	if (rc >= 0) {
		rs.n = rs.file_absent = 0;
		if (rec_save(path, &rs) < 0)
			rc = -1;
	}
	free(out);
	free(t);
	return rc;
}

static const char *
ly_config(Style *st)
{
	if (exists("/etc/ly/config.lua")) {
		*st = ST_LUA;
		return "/etc/ly/config.lua";
	}
	*st = ST_INI;
	return exists("/etc/ly/config.ini") ? "/etc/ly/config.ini" : NULL;
}

/* Where the boot reads the console font, by the system:
 *   systemd with its vconsole setup (Arch, Fedora, openSUSE...): /etc/vconsole.conf,
 *     FONT=<name>
 *   Debian, Ubuntu (systemd without it, console-setup instead):
 *     /etc/default/console-setup, FONT="<the font's file>"
 *   OpenRC (Gentoo, Alpine, Artix): /etc/conf.d/consolefont, consolefont="<name>"
 *   Void (runit): /etc/rc.conf, FONT="<name>"
 * The file it goes in, with *want filled; NULL when console-setup wants a file that
 * is not installed. */
static const char *const font_places[] = {
	"/etc/vconsole.conf", "/etc/default/console-setup", "/etc/conf.d/consolefont", "/etc/rc.conf",
};

static const char *
console_font_place(const char *font, Want *want)
{
	static const char *const vsetup[] = { "/usr/lib/systemd/systemd-vconsole-setup",
		"/lib/systemd/systemd-vconsole-setup" };

	for (size_t i = 0; i < TM_COUNT(vsetup); i++)
		if (exists(vsetup[i])) {
			want->key = "FONT";
			snprintf(want->val, sizeof want->val, "%s", font);
			return font_places[0];
		}
	if (exists(font_places[1])) {
		static const char *const dirs[] = { "/usr/share/consolefonts", "/usr/share/kbd/consolefonts" };
		static const char *const ext[] = { ".psf.gz", ".psfu.gz", ".psf", ".psfu" };
		char p[PATH_MAX];

		for (size_t d = 0; d < TM_COUNT(dirs); d++)
			for (size_t e = 0; e < TM_COUNT(ext); e++) {
				snprintf(p, sizeof p, "%s/%s%s", dirs[d], font, ext[e]);
				if (exists(p)) {
					want->key = "FONT";
					snprintf(want->val, sizeof want->val, "\"%.64s%s\"", font, d ? "" : ext[e]);
					if (d)          /* outside console-setup's own directory: the path */
						snprintf(want->val, sizeof want->val, "\"%.80s\"", p);
					return font_places[1];
				}
			}
		return NULL;
	}
	if (exists(font_places[2])) {
		want->key = "consolefont";
		snprintf(want->val, sizeof want->val, "\"%.64s\"", font);
		return font_places[2];
	}
	if (exists(font_places[3]) && (exists("/etc/runit") || exists("/etc/sv"))) {
		want->key = "FONT";
		snprintf(want->val, sizeof want->val, "\"%.64s\"", font);
		return font_places[3];
	}
	want->key = "FONT";
	snprintf(want->val, sizeof want->val, "%s", font);
	return font_places[0];
}

/* ---- the sync ---- */

/* the user it was installed for, or "" */
static void
installed_user(char *out, size_t n)
{
	char *t = etc_read(USER_FILE);

	out[0] = '\0';
	if (t) {
		t[strcspn(t, "\n")] = '\0';
		if (valid_user(t))
			snprintf(out, n, "%s", t);
		free(t);
	}
}

static int
sync_who(const Who *w)
{
	char dir[PATH_MAX], f[PATH_MAX + 32], text[USER_MAX], v[96], pal[1024];
	int got, bad = 0;
	Style lst;
	const char *ly;

	snprintf(dir, sizeof dir, "%s/.config", w->home);
	/* at boot the home may not be there yet (not mounted, not decrypted): then the
	 * palette kept in /etc is used, and nothing is taken for removed. A home that is
	 * there without the directory is one whose styles were removed. */
	if (user_read(w, dir, NULL, 0) != 1) {
		if (exists(ETC_DIR "/tty.palette"))
			spawn("setvtrgb", ETC_DIR "/tty.palette", NULL, NULL, NULL);
		return 0;
	}
	snprintf(dir, sizeof dir, "%s/.config/themer/system", w->home);

	/* the console palette */
	snprintf(f, sizeof f, "%s/tty.palette", dir);
	got = user_read(w, f, text, sizeof text);
	if (got >= 0 && parse_palette(text, pal, sizeof pal) == 0) {
		char *cur = etc_read(ETC_DIR "/tty.palette");

		if (!cur || strcmp(cur, pal)) {
			mkdirs(ETC_DIR);
			chmod(ETC_DIR, 0755);
			if (etc_write(ETC_DIR "/tty.palette", pal, 0644) < 0)
				bad = 1;
		}
		free(cur);
		spawn("setvtrgb", ETC_DIR "/tty.palette", NULL, NULL, NULL);
	} else if (got == UR_ABSENT) {
		if (exists(ETC_DIR "/tty.palette")) {
			unlink(ETC_DIR "/tty.palette");
			spawn("setvtrgb", "vga", NULL, NULL, NULL);
		}
	} else {
		fprintf(stderr, "themer: %s: not a palette, left as it was\n", f);
		bad = 1;
	}

	/* the console font, where this system's boot reads it */
	snprintf(f, sizeof f, "%s/console.conf", dir);
	got = user_read(w, f, text, sizeof text);
	if (got >= 0 && text_value(text, "FONT", v, sizeof v) == 0 && v[0]) {
		Want want;
		const char *file;

		if (!valid_font(v)) {
			fprintf(stderr, "themer: %s: FONT is not a font's name, left as it was\n", f);
			bad = 1;
		} else if (!(file = console_font_place(v, &want))) {
			fprintf(stderr, "themer: the console font %s is not installed here (/usr/share/consolefonts), "
			        "left as it was\n", v);
		} else {
			/* the other places, from a system that changed (or an earlier build) */
			for (size_t k = 0; k < TM_COUNT(font_places); k++)
				if (strcmp(font_places[k], file) && etc_restore(font_places[k], ST_SH) < 0)
					bad = 1;
			if (etc_apply(file, ST_SH, &want, 1) < 0)
				bad = 1;
		}
	} else if (got == UR_ABSENT || got >= 0) {
		for (size_t k = 0; k < TM_COUNT(font_places); k++)
			if (etc_restore(font_places[k], ST_SH) < 0)
				bad = 1;
	} else {
		bad = 1;
	}

	/* ly's colors */
	snprintf(f, sizeof f, "%s/ly.conf", dir);
	if ((ly = ly_config(&lst))) {
		got = user_read(w, f, text, sizeof text);
		if (got >= 0) {
			Want want[TM_COUNT(ly_keys)];
			int n = 0, wrong = !ly_lines_ok(text);

			for (size_t k = 0; k < TM_COUNT(ly_keys) && !wrong; k++) {
				int r = text_value(text, ly_keys[k], v, sizeof v);

				if (r == 0 && valid_ly_color(v)) {
					want[n].key = ly_keys[k];
					snprintf(want[n++].val, sizeof want[0].val, "%s", v);
				} else if (r != -1) {
					wrong = 1;
				}
			}
			if (wrong) {
				fprintf(stderr, "themer: %s: not ly's colors, left as they were\n", f);
				bad = 1;
			} else if (etc_apply(ly, lst, want, n) < 0) {
				bad = 1;
			}
		} else if (got == UR_ABSENT) {
			if (etc_restore(ly, lst) < 0)
				bad = 1;
		} else {
			bad = 1;
		}
	}
	return bad;
}

static int
sync_user(const char *user)
{
	char inst[64];
	Who w;

	installed_user(inst, sizeof inst);
	if (!inst[0] || strcmp(inst, user)) {
		if (inst[0])
			fprintf(stderr, "themer: installed for %s, not %s\n", inst, user);
		else
			fprintf(stderr, "themer: not installed (sudo themer system install %s)\n", user);
		return 1;
	}
	if (who(user, &w) < 0) {
		fprintf(stderr, "themer: no user %s\n", user);
		return 1;
	}
	return sync_who(&w);
}

/* ---- install, remove, status ---- */

/* the unit runs this binary as root, so neither it nor any directory above it may
 * be one anyone but root can change */
static int
safe_binary(const char *path)
{
	char p[PATH_MAX], *slash;
	struct stat st;

	snprintf(p, sizeof p, "%s", path);
	for (;;) {
		/* a sticky directory (/tmp) lets no one else replace root's entries */
		if (stat(p[0] ? p : "/", &st) < 0 || st.st_uid != 0 ||
		    ((st.st_mode & (S_IWGRP | S_IWOTH)) && !(S_ISDIR(st.st_mode) && (st.st_mode & S_ISVTX))))
			return 0;
		if (!p[0] || !(slash = strrchr(p, '/')))
			return 1;
		*slash = '\0';
	}
}


/* ---- watch: the service for init systems without systemd's path units ---- */

static volatile sig_atomic_t stop_watch;

static void
on_term(int sig)
{
	(void)sig;
	stop_watch = 1;
}

/* syncs, then again whenever the user's system directory changes. While the
 * directory (or the home) is not there yet, it watches the nearest parent that is
 * and looks again every half minute, which covers a home mounted after boot. */
static int
watch_user(const char *user)
{
	char inst[64], dirs[3][PATH_MAX];
	struct sigaction sa;
	Who w;

	installed_user(inst, sizeof inst);
	if (!inst[0] || strcmp(inst, user) || who(user, &w) < 0) {
		fprintf(stderr, "themer: not installed for %s\n", user);
		return 1;
	}
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_term;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	snprintf(dirs[0], sizeof dirs[0], "%s/.config/themer/system", w.home);
	snprintf(dirs[1], sizeof dirs[1], "%s/.config/themer", w.home);
	snprintf(dirs[2], sizeof dirs[2], "%s/.config", w.home);
	while (!stop_watch) {
		int fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK), wd = -1, level;
		struct pollfd pfd;
		char ev[4096];

		sync_who(&w);
		if (fd < 0) {
			perror("themer: inotify");
			return 1;
		}
		for (level = 0; level < 3 && wd < 0; level++)
			wd = inotify_add_watch(fd, dirs[level], IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM | IN_CREATE |
			                       IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF | IN_DONT_FOLLOW | IN_ONLYDIR);
		pfd.fd = fd;
		pfd.events = POLLIN;
		/* watching the directory itself: until it changes; a parent: a look now and then */
		if (poll(&pfd, 1, wd >= 0 && level == 1 ? -1 : 30000) > 0) {
			/* an apply writes several files: let it finish, then sync once */
			do {
				while (read(fd, ev, sizeof ev) > 0)
					;
			} while (poll(&pfd, 1, 300) > 0 && !stop_watch);
		}
		close(fd);
	}
	return 0;
}

/* ---- the init systems ---- */

typedef enum { INIT_NONE, INIT_SYSTEMD, INIT_OPENRC, INIT_RUNIT, INIT_S6, INIT_DINIT, INIT_SYSV } Init;
static const char *const init_names[] = { "none", "systemd", "openrc", "runit", "s6", "dinit", "sysv" };

static int
init_from_name(const char *n)
{
	for (size_t i = 0; i < TM_COUNT(init_names); i++)
		if (!strcmp(n, init_names[i]))
			return (int)i;
	return -1;
}

/* the init system running; THEMER_INIT names one instead (for tests) */
static Init
init_detect(void)
{
	char comm[64] = "";
	const char *force = getenv("THEMER_INIT");
	FILE *f;

	if (force && init_from_name(force) >= 0)
		return (Init)init_from_name(force);
	if (exists("/run/systemd/system"))
		return INIT_SYSTEMD;
	if (exists("/run/openrc"))
		return INIT_OPENRC;
	if ((f = fopen("/proc/1/comm", "r"))) {
		if (fgets(comm, sizeof comm, f))
			comm[strcspn(comm, "\n")] = '\0';
		fclose(f);
	}
	if (!strcmp(comm, "dinit"))
		return INIT_DINIT;
	if (!strncmp(comm, "s6-svscan", 9) && exists("/etc/s6/adminsv"))
		return INIT_S6;
	if (!strcmp(comm, "runit") || exists("/run/runit"))
		return INIT_RUNIT;
	if (exists("/etc/init.d") && (exists("/etc/inittab") || exists("/etc/rc.d") || exists("/etc/rc2.d")))
		return INIT_SYSV;
	return INIT_NONE;
}

/* where runit keeps services and where a link enables one (Void: /etc/sv and
 * /var/service; Artix: /etc/runit/sv and /run/runit/service) */
static void
runit_dirs(char *sv, char *on, size_t n)
{
	snprintf(sv, n, "%s", exists("/etc/sv") ? "/etc/sv" : "/etc/runit/sv");
	snprintf(on, n, "%s", exists("/var/service") ? "/var/service" :
	         exists("/etc/runit/runsvdir/default") ? "/etc/runit/runsvdir/default" : "/run/runit/service");
}

/* makes a directory in /etc and its missing parents, noting each one it made in
 * /etc/themer/made so remove takes away only those */
static int
etc_mkdirs(const char *path)
{
	char p[PATH_MAX], made[8 * PATH_MAX] = "", *old;
	size_t o;

	snprintf(p, sizeof p, "%s", path);
	for (char *s = p + 1; ; s++) {
		if (*s == '/' || !*s) {
			char c = *s;

			*s = '\0';
			if (!exists(p)) {
				if (mkdir(p, 0755) < 0)
					return -1;
				o = strlen(made);
				snprintf(made + o, sizeof made - o, "%s\n", p);
			}
			*s = c;
			if (!c)
				break;
		}
	}
	if (!made[0])
		return 0;
	if ((old = etc_read(ETC_DIR "/made"))) {
		char all[9 * PATH_MAX];

		snprintf(all, sizeof all, "%s%s", old, made);
		free(old);
		return etc_write(ETC_DIR "/made", all, 0644);
	}
	return etc_write(ETC_DIR "/made", made, 0644);
}

/* takes away the directories etc_mkdirs made, when they are empty, deepest first */
static void
etc_rmdirs(void)
{
	char *t = etc_read(ETC_DIR "/made"), *l[256];
	int n = 0;

	if (!t)
		return;
	for (char *s = strtok(t, "\n"); s && n < 256; s = strtok(NULL, "\n"))
		if (!strncmp(s, "/etc/", 5) || !strncmp(s, "/run/", 5) || !strncmp(s, "/var/", 5))
			l[n++] = s;
	while (n--)
		rmdir(l[n]);
	free(t);
	unlink(ETC_DIR "/made");
}

static int
write_exec(const char *path, const char *text)
{
	return etc_write(path, text, 0755) < 0 || chmod(path, 0755) < 0 ? -1 : 0;
}

/* writes and starts the service for this init; -1 when it could not */
static int
service_install(Init init, const char *user, const char *self, const char *dir)
{
	char name[96], p[PATH_MAX], q[PATH_MAX + 16], text[4 * PATH_MAX];

	snprintf(name, sizeof name, "themer-system-%s", user);
	switch (init) {
	case INIT_SYSTEMD:
		snprintf(p, sizeof p, "/etc/systemd/system/%s.service", name);
		snprintf(text, sizeof text,
		         "# generated by themer\n[Unit]\nDescription=themer: the console and ly in %s's theme\n"
		         "After=local-fs.target\n\n[Service]\nType=oneshot\nExecStart=%s system sync %s\n\n"
		         "[Install]\nWantedBy=multi-user.target\n", user, self, user);
		if (etc_write(p, text, 0644) < 0)
			return -1;
		snprintf(p, sizeof p, "/etc/systemd/system/%s.path", name);
		snprintf(text, sizeof text,
		         "# generated by themer\n[Unit]\nDescription=themer: watch %s's theme for the console and ly\n\n"
		         "[Path]\nPathChanged=%s\nPathModified=%s\nUnit=%s.service\n\n"
		         "[Install]\nWantedBy=paths.target\n", user, dir, dir, name);
		if (etc_write(p, text, 0644) < 0)
			return -1;
		spawn("systemctl", "daemon-reload", NULL, NULL, NULL);
		snprintf(p, sizeof p, "%s.service", name);
		snprintf(q, sizeof q, "%s.path", name);
		return spawn("systemctl", "enable", p, NULL, NULL) < 0 || spawn("systemctl", "enable", "--now", q, NULL) < 0 ? -1 : 0;
	case INIT_OPENRC:
		snprintf(p, sizeof p, "/etc/init.d/%s", name);
		snprintf(text, sizeof text,
		         "#!/sbin/openrc-run\n# generated by themer\n"
		         "description=\"themer: the console and ly in %s's theme\"\n"
		         "command=\"%s\"\ncommand_args=\"system watch %s\"\ncommand_background=true\n"
		         "pidfile=\"/run/${RC_SVCNAME}.pid\"\n\ndepend() {\n\tneed localmount\n\tafter bootmisc\n}\n",
		         user, self, user);
		if (write_exec(p, text) < 0)
			return -1;
		spawn("rc-update", "add", name, "default", NULL);
		return spawn("rc-service", name, "start", NULL, NULL);
	case INIT_RUNIT: {
		char sv[PATH_MAX / 2], on[PATH_MAX / 2];

		runit_dirs(sv, on, sizeof sv);
		snprintf(p, sizeof p, "%s/%s", sv, name);
		if (etc_mkdirs(p) < 0)
			return -1;
		snprintf(q, sizeof q, "%s/run", p);
		snprintf(text, sizeof text, "#!/bin/sh\n# generated by themer\nexec %s system watch %s 2>&1\n", self, user);
		if (write_exec(q, text) < 0)
			return -1;
		snprintf(q, sizeof q, "%s/%s", on, name);
		etc_mkdirs(on);
		return symlink(p, q) < 0 && errno != EEXIST ? -1 : 0;     /* runsvdir starts it */
	}
	case INIT_S6:
		snprintf(p, sizeof p, "/etc/s6/sv/%s", name);
		if (etc_mkdirs(p) < 0)
			return -1;
		snprintf(q, sizeof q, "%s/type", p);
		if (etc_write(q, "longrun\n", 0644) < 0)
			return -1;
		snprintf(q, sizeof q, "%s/run", p);
		snprintf(text, sizeof text, "#!/bin/sh\n# generated by themer\nexec %s system watch %s 2>&1\n", self, user);
		if (write_exec(q, text) < 0)
			return -1;
		snprintf(q, sizeof q, "/etc/s6/adminsv/default/contents.d/%s", name);
		if (etc_write(q, "", 0644) < 0)
			return -1;
		spawn("s6-db-reload", NULL, NULL, NULL, NULL);
		return spawn("s6-rc", "-u", "change", name, NULL);
	case INIT_DINIT:
		snprintf(p, sizeof p, "/etc/dinit.d/%s", name);
		if (etc_mkdirs("/etc/dinit.d") < 0)
			return -1;
		snprintf(text, sizeof text, "# generated by themer: the console and ly in %s's theme\n"
		         "type = process\ncommand = %s system watch %s\nrestart = true\n", user, self, user);
		if (etc_write(p, text, 0644) < 0)
			return -1;
		return spawn("dinitctl", "enable", name, NULL, NULL);
	case INIT_SYSV:
		snprintf(p, sizeof p, "/etc/init.d/%s", name);
		snprintf(text, sizeof text,
		         "#!/bin/sh\n# generated by themer\n### BEGIN INIT INFO\n# Provides:          %s\n"
		         "# Required-Start:    $local_fs $remote_fs\n# Required-Stop:     $local_fs $remote_fs\n"
		         "# Default-Start:     2 3 4 5\n# Default-Stop:      0 1 6\n"
		         "# Short-Description: themer: the console and ly in %s's theme\n### END INIT INFO\n"
		         "P=/run/%s.pid\nrunning() { [ -f $P ] && kill -0 \"$(cat $P)\" 2>/dev/null; }\n"
		         "case \"$1\" in\nstart) running && exit 0\n\t%s system watch %s </dev/null >/dev/null 2>&1 &\n\techo $! > $P ;;\n"
		         "stop) running && kill \"$(cat $P)\"; rm -f $P ;;\nrestart) \"$0\" stop; \"$0\" start ;;\n"
		         "status) if running; then echo running; else echo stopped; exit 3; fi ;;\n"
		         "*) echo \"usage: $0 start|stop|restart|status\"; exit 2 ;;\nesac\n",
		         name, user, name, self, user);
		if (write_exec(p, text) < 0)
			return -1;
		if (spawn("update-rc.d", name, "defaults", NULL, NULL) < 0 && spawn("chkconfig", "--add", name, NULL, NULL) < 0)
			fprintf(stderr, "themer: neither update-rc.d nor chkconfig: link %s into your runlevels\n", p);
		return spawn(p, "start", NULL, NULL, NULL);
	case INIT_NONE:
		break;
	}
	return -1;
}

static void
service_remove(Init init, const char *user)
{
	char name[96], p[PATH_MAX], q[PATH_MAX];

	snprintf(name, sizeof name, "themer-system-%s", user);
	switch (init) {
	case INIT_SYSTEMD:
		snprintf(p, sizeof p, "%s.path", name);
		snprintf(q, sizeof q, "%s.service", name);
		spawn("systemctl", "disable", "--now", p, q);
		snprintf(p, sizeof p, "/etc/systemd/system/%s.path", name);
		snprintf(q, sizeof q, "/etc/systemd/system/%s.service", name);
		unlink(p);
		unlink(q);
		spawn("systemctl", "daemon-reload", NULL, NULL, NULL);
		break;
	case INIT_OPENRC:
		spawn("rc-service", name, "stop", NULL, NULL);
		spawn("rc-update", "del", name, "default", NULL);
		snprintf(p, sizeof p, "/etc/init.d/%s", name);
		unlink(p);
		break;
	case INIT_RUNIT: {
		char sv[PATH_MAX / 2], on[PATH_MAX / 2];

		runit_dirs(sv, on, sizeof sv);
		snprintf(q, sizeof q, "%s/%s", on, name);
		spawn("sv", "down", q, NULL, NULL);
		unlink(q);
		snprintf(p, sizeof p, "%s/%s", sv, name);
		spawn("rm", "-rf", "--", p, NULL);
		break;
	}
	case INIT_S6:
		spawn("s6-rc", "-d", "change", name, NULL);
		snprintf(q, sizeof q, "/etc/s6/adminsv/default/contents.d/%s", name);
		unlink(q);
		spawn("s6-db-reload", NULL, NULL, NULL, NULL);
		snprintf(p, sizeof p, "/etc/s6/sv/%s", name);
		spawn("rm", "-rf", "--", p, NULL);
		break;
	case INIT_DINIT:
		spawn("dinitctl", "disable", name, NULL, NULL);
		spawn("dinitctl", "stop", name, NULL, NULL);
		snprintf(p, sizeof p, "/etc/dinit.d/%s", name);
		unlink(p);
		break;
	case INIT_SYSV:
		snprintf(p, sizeof p, "/etc/init.d/%s", name);
		spawn(p, "stop", NULL, NULL, NULL);
		if (spawn("update-rc.d", name, "remove", NULL, NULL) < 0)
			spawn("chkconfig", "--del", name, NULL, NULL);
		unlink(p);
		break;
	case INIT_NONE:
		break;
	}
}

/* the init the service was installed for, from /etc/themer/init */
static Init
init_installed(void)
{
	char *t = etc_read(ETC_DIR "/init");
	int i = -1;

	if (t) {
		t[strcspn(t, "\n")] = '\0';
		i = init_from_name(t);
		free(t);
	}
	return i < 0 ? INIT_NONE : (Init)i;
}

/* ---- install, remove, status ---- */

static int
install(const char *user)
{
	char dir[PATH_MAX], self[PATH_MAX], inst[64];
	ssize_t len;
	Init init = init_detect();
	Who w;

	if (who(user, &w) < 0) {
		fprintf(stderr, "themer: no user %s\n", user);
		return 1;
	}
	installed_user(inst, sizeof inst);
	if (inst[0] && strcmp(inst, user)) {
		fprintf(stderr, "themer: already installed for %s (one user at a time): sudo themer system remove %s first\n",
		        inst, inst);
		return 1;
	}
	if ((len = readlink("/proc/self/exe", self, sizeof self - 1)) < 0) {
		perror("themer: /proc/self/exe");
		return 1;
	}
	self[len] = '\0';
	if (!safe_binary(self)) {
		fprintf(stderr, "themer: %s is not root's alone, and the service would run it as root.\n"
		        "Install themer first (sudo make install), then run the installed one:\n"
		        "  sudo themer system install %s\n", self, user);
		return 1;
	}
	snprintf(dir, sizeof dir, "%s/.config/themer/system", w.home);
	if (user_mkdir(&w, dir) < 0)
		fprintf(stderr, "themer: could not make %s as %s\n", dir, user);
	if (inst[0] && exists(ETC_DIR "/init") && init_installed() != init)
		service_remove(init_installed(), user);     /* installed before for another init */
	if (mkdirs(ETC_DIR) < 0 || chmod(ETC_DIR, 0755) < 0 || etc_write(USER_FILE, user, 0644) < 0 ||
	    etc_write(ETC_DIR "/init", init_names[init], 0644) < 0) {
		perror("themer: " ETC_DIR);
		return 1;
	}
	if (init == INIT_NONE) {
		printf("no init system themer knows here (systemd, OpenRC, runit, s6, dinit, SysV): synced once.\n"
		       "To keep the console and ly in step, have your init run, as root, at boot:\n"
		       "  %s system watch %s\n", self, user);
		return sync_user(user);
	}
	/* the first sync before the service, so it starts from a known state */
	sync_user(user);
	if (service_install(init, user, self, dir) < 0)
		fprintf(stderr, "themer: the %s service is written but could not be started\n", init_names[init]);
	printf("installed (%s): the console palette and font and ly follow %s's theme\n"
	       "(sudo themer system remove puts them back)\n", init_names[init], user);
	return 0;
}

static int
uninstall(const char *user)
{
	char inst[64];
	Style lst;
	const char *ly = ly_config(&lst);
	int bad = 0;

	installed_user(inst, sizeof inst);
	if (inst[0] && strcmp(inst, user)) {
		fprintf(stderr, "themer: installed for %s, not %s\n", inst, user);
		return 1;
	}
	service_remove(exists(ETC_DIR "/init") ? init_installed() : INIT_SYSTEMD, user);
	for (size_t k = 0; k < TM_COUNT(font_places); k++)
		if (etc_restore(font_places[k], ST_SH) < 0)
			bad = 1;
	if (ly && etc_restore(ly, lst) < 0)
		bad = 1;
	if (exists(ETC_DIR "/tty.palette")) {
		unlink(ETC_DIR "/tty.palette");
		spawn("setvtrgb", "vga", NULL, NULL, NULL);
	}
	if (bad) {
		fprintf(stderr, "themer: not everything could be put back; what is left is recorded in " BACKUP_DIR "\n");
		return 1;
	}
	etc_rmdirs();
	unlink(USER_FILE);
	unlink(ETC_DIR "/init");
	rmdir(BACKUP_DIR);
	rmdir(ETC_DIR);
	printf("removed: the console and ly are as they were\n");
	return 0;
}

static int
status(const char *user)
{
	char inst[64];
	Style lst;
	const char *ly = ly_config(&lst);

	(void)user;
	installed_user(inst, sizeof inst);
	printf("user:     %s\n", inst[0] ? inst : "none (sudo themer system install)");
	printf("service:  %s\n", inst[0] ? init_names[init_installed()] : "none");
	printf("init:     %s\n", init_names[init_detect()]);
	printf("palette:  %s\n", exists(ETC_DIR "/tty.palette") ? ETC_DIR "/tty.palette" : "the console's own");
	printf("ly:       %s\n", ly ? ly : "not installed");
	return 0;
}

int
system_cmd(int argc, char **argv)
{
	const char *sub = argc > 2 ? argv[2] : "status";
	const char *user = argc > 3 ? argv[3] : getenv("SUDO_USER");

	if (!user || !*user) {
		struct passwd *pw = getpwuid(getuid());

		user = pw ? pw->pw_name : getenv("USER");
	}
	if (!user || !*user) {
		fprintf(stderr, "themer: whose theme? themer system %s USER\n", sub);
		return 1;
	}
	if (!valid_user(user)) {
		fprintf(stderr, "themer: %s is not a user's name\n", user);
		return 1;
	}
	if (!strcmp(sub, "status"))
		return status(user);
	if (strcmp(sub, "install") && strcmp(sub, "remove") && strcmp(sub, "sync") && strcmp(sub, "watch")) {
		fprintf(stderr, "usage: themer system install|remove|sync|watch|status [USER]\n");
		return 1;
	}
	if (getuid() != 0) {
		fprintf(stderr, "themer: system %s changes files in /etc: run it with sudo\n", sub);
		return 1;
	}
	umask(022);
	if (!strcmp(sub, "install"))
		return install(user);
	if (!strcmp(sub, "remove"))
		return uninstall(user);
	if (!strcmp(sub, "watch"))
		return watch_user(user);
	return sync_user(user);
}
