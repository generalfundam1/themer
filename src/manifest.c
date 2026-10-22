/* manifest.c: the tm_* helpers modules write files with, and the manifest that
 * makes every write undoable. A manifest lives in ~/.local/state/themer/<name>.manifest,
 * one record per line, fields separated by tabs:
 *   F path                       file written whole by tm_writef (has the marker)
 *   C path                       file created by a block or ini edit; removed
 *                                on undo only if it ends up empty
 *   D path                       directory created
 *   B path id prefix suffix      marker block inserted
 *   L path prefix - old          a line replaced by tm_setline; old is the line it replaced
 *   X path                       file replaced by tm_replacef; the user's
 *                                original was moved to path.themer-orig
 *   K path section key old       ini key set; old is the previous value,
 *                                "" means the key was absent, NEW_SECTION
 *                                means the section itself was created
 *   A path                       a file copied by tm_copy (an asset: an image, a
 *                                font); removed on undo
 *   S path                       a symbolic link made by tm_symlink; removed on undo
 */
#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "themer.h"

#define MAX_RECS    512
#define MAX_BODY    (2 * 1024 * 1024)   /* a block: the userChrome stylesheet with its icons */
#define NEW_SECTION       "<new>"       /* section was created */
#define NEW_SECTION_BLANK "<new+blank>" /* ...and a blank line was added before it */

enum { R_FILE = 'F', R_CREATED = 'C', R_DIR = 'D', R_BLOCK = 'B', R_KEY = 'K', R_REPLACE = 'X', R_LINE = 'L', R_ASSET = 'A', R_LINK = 'S' };

#define BACKUP_SUFFIX ".themer-orig"

typedef struct {
	char kind;
	char path[PATH_MAX];
	char s[3][256];
} Rec;

typedef struct { char **v; int n, cap; } Lines;

static int find_setline(const Lines *L, const char *prefix);

static Rec recs[MAX_RECS];
static int nrecs, recording;

static const char *
home(void)
{
	const char *h = getenv("HOME");

	return h ? h : "";
}

static const char *
skip_ws(const char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	return s;
}

static int
blank(const char *s)
{
	return !*skip_ws(s);
}

static int
is_header(const char *s)
{
	return *skip_ws(s) == '[';
}

static void
trim_right(char *s)
{
	size_t n = strlen(s);

	while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r'))
		s[--n] = '\0';
}

/* ---- records ---- */

/* only recorded while a module's load() runs; undo never records */
static void
rec_add(char kind, const char *path, const char *a, const char *b, const char *c)
{
	Rec *r;
	int i;

	if (!recording || nrecs >= MAX_RECS)
		return;
	for (i = 0; i < nrecs; i++)
		if (recs[i].kind == kind && !strcmp(recs[i].path, path) &&
		    !strcmp(recs[i].s[0], a) && !strcmp(recs[i].s[1], b))
			return;
	r = &recs[nrecs++];
	r->kind = kind;
	snprintf(r->path, sizeof r->path, "%s", path);
	snprintf(r->s[0], sizeof r->s[0], "%s", a);
	snprintf(r->s[1], sizeof r->s[1], "%s", b);
	snprintf(r->s[2], sizeof r->s[2], "%s", c);
}

static int
manifest_path(char *p, size_t n, const char *mod)
{
	return snprintf(p, n, "%s/.local/state/themer/%s.manifest", home(), mod) < (int)n ? 0 : -1;
}

static int
manifest_write(const char *mod)
{
	char p[PATH_MAX];
	FILE *f;
	int i;

	if (manifest_path(p, sizeof p, mod) < 0)
		return -1;
	if (!nrecs) {
		unlink(p);
		return 0;
	}
	{
		char dir[PATH_MAX];

		snprintf(dir, sizeof dir, "%s/.local/state/themer", home());
		tm_mkdirs(dir);
	}
	if (!(f = fopen(p, "w")))
		return -1;
	for (i = 0; i < nrecs; i++)
		fprintf(f, "%c\t%s\t%s\t%s\t%s\n", recs[i].kind, recs[i].path,
		        recs[i].s[0], recs[i].s[1], recs[i].s[2]);
	return fclose(f) == 0 ? 0 : -1;
}

/* splits s in place on tabs; empty fields are kept */
static int
split_tabs(char *s, char **f, int max)
{
	int k = 0;

	f[k++] = s;
	for (; *s; s++)
		if (*s == '\t') {
			*s = '\0';
			if (k < max)
				f[k++] = s + 1;
		}
	return k;
}

static int
manifest_read(const char *mod)
{
	static const char empty[] = "";
	char p[PATH_MAX], *line = NULL;
	size_t cap = 0;
	FILE *f;

	if (manifest_path(p, sizeof p, mod) < 0 || !(f = fopen(p, "r")))
		return -1;
	nrecs = 0;
	while (nrecs < MAX_RECS && getline(&line, &cap, f) > 0) {
		char *fld[5] = { 0 };
		int k, i;
		Rec *r;

		line[strcspn(line, "\r\n")] = '\0';
		if (!line[0])
			continue;
		k = split_tabs(line, fld, 5);
		for (i = k; i < 5; i++)
			fld[i] = (char *)empty;
		if (!fld[1][0])
			continue;
		r = &recs[nrecs++];
		r->kind = fld[0][0];
		snprintf(r->path, sizeof r->path, "%s", fld[1]);
		snprintf(r->s[0], sizeof r->s[0], "%s", fld[2]);
		snprintf(r->s[1], sizeof r->s[1], "%s", fld[3]);
		snprintf(r->s[2], sizeof r->s[2], "%s", fld[4]);
	}
	free(line);
	fclose(f);
	return 0;
}

/* ---- line buffers for file edits ---- */

static int
lines_read(const char *path, Lines *L)
{
	FILE *f = fopen(path, "r");
	char *line = NULL;
	size_t cap = 0;

	memset(L, 0, sizeof *L);
	if (!f)
		return -1;
	while (getline(&line, &cap, f) > 0) {
		line[strcspn(line, "\r\n")] = '\0';
		if (L->n == L->cap) {
			L->cap = L->cap ? L->cap * 2 : 64;
			L->v = realloc(L->v, L->cap * sizeof *L->v);
		}
		L->v[L->n++] = strdup(line);
	}
	free(line);
	fclose(f);
	return 0;
}

static void
lines_ins(Lines *L, int at, const char *s)
{
	if (L->n == L->cap) {
		L->cap = L->cap ? L->cap * 2 : 64;
		L->v = realloc(L->v, L->cap * sizeof *L->v);
	}
	memmove(&L->v[at + 1], &L->v[at], (L->n - at) * sizeof *L->v);
	L->v[at] = strdup(s);
	L->n++;
}

static void
lines_del(Lines *L, int at)
{
	free(L->v[at]);
	memmove(&L->v[at], &L->v[at + 1], (L->n - at - 1) * sizeof *L->v);
	L->n--;
}

static void
lines_free(Lines *L)
{
	for (int i = 0; i < L->n; i++)
		free(L->v[i]);
	free(L->v);
	memset(L, 0, sizeof *L);
}

static int
lines_write(const char *path, const Lines *L)
{
	char tmp[PATH_MAX];
	FILE *f;
	int i;

	snprintf(tmp, sizeof tmp, "%s.themer", path);
	if (!(f = fopen(tmp, "w")))
		return -1;
	for (i = 0; i < L->n; i++)
		fprintf(f, "%s\n", L->v[i]);
	if (fclose(f) != 0)
		return -1;
	return rename(tmp, path);
}

/* ---- directories ---- */

static int
mkdir_one(const char *d)
{
	struct stat st;

	if (!stat(d, &st))
		return S_ISDIR(st.st_mode) ? 0 : -1;
	if (mkdir(d, 0755) < 0)
		return -1;
	rec_add(R_DIR, d, "", "", "");
	return 0;
}

int
tm_mkdirs(const char *path)
{
	char buf[PATH_MAX], *p;

	snprintf(buf, sizeof buf, "%s", path);
	for (p = buf + 1; *p; p++)
		if (*p == '/') {
			*p = '\0';
			if (mkdir_one(buf) < 0)
				return -1;
			*p = '/';
		}
	return mkdir_one(buf);
}

static int
mkdirs_parent(const char *path)
{
	char buf[PATH_MAX], *slash;

	snprintf(buf, sizeof buf, "%s", path);
	slash = strrchr(buf, '/');
	if (!slash || slash == buf)
		return 0;
	*slash = '\0';
	return tm_mkdirs(buf);
}

/* ---- public helpers ---- */

int
tm_path(char *buf, size_t n, const TmTheme *t, const char *rel)
{
	return snprintf(buf, n, "%s/%s", t->home, rel) < (int)n ? 0 : -1;
}

int
tm_have(const char *prog)
{
	const char *env = getenv("PATH");
	char *dup, *tok, full[PATH_MAX];
	int found = 0;

	if (!env)
		return 0;
	dup = strdup(env);
	for (tok = strtok(dup, ":"); tok && !found; tok = strtok(NULL, ":")) {
		snprintf(full, sizeof full, "%s/%s", tok, prog);
		found = !access(full, X_OK);
	}
	free(dup);
	return found;
}

/* true if the path was recorded as an asset of the module being loaded (the records
 * read back before undo), so a leftover copy of ours is never mistaken for a user file */
static int
was_asset(const char *path)
{
	for (int i = 0; i < nrecs; i++)
		if (recs[i].kind == R_ASSET && !strcmp(recs[i].path, path))
			return 1;
	return 0;
}

/* true if the file starts with the themer marker in its first few lines
 * (scripts have a #! line first, so the marker is not always on line one) */
static int
is_ours(const char *path)
{
	char head[512] = "";
	size_t n;
	FILE *f = fopen(path, "r");

	if (!f)
		return 0;
	n = fread(head, 1, sizeof head - 1, f);
	fclose(f);
	head[n] = '\0';
	return strstr(head, "generated by themer") != NULL;
}

int
tm_writef(const char *path, const char *fmt, ...)
{
	va_list ap;
	FILE *f;

	if (!access(path, F_OK) && !is_ours(path)) {
		fprintf(stderr, "themer: not overwriting %s (not generated by themer)\n", path);
		return -1;
	}
	if (mkdirs_parent(path) < 0 || !(f = fopen(path, "w")))
		return -1;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	if (fclose(f) != 0)
		return -1;
	rec_add(R_FILE, path, "", "", "");
	return 0;
}

/* Copies a file as it is. The copy is recorded and removed on unload. A file
 * already at dst that is not one of ours is left alone, with a message. */
int
tm_copy(const char *src, const char *dst)
{
	static unsigned char buf[65536];
	FILE *in, *out;
	size_t n;

	if (!access(dst, F_OK) && !is_ours(dst) && !was_asset(dst)) {
		fprintf(stderr, "themer: not overwriting %s (not placed by themer)\n", dst);
		return -1;
	}
	if (!(in = fopen(src, "rb")))
		return -1;
	if (mkdirs_parent(dst) < 0 || !(out = fopen(dst, "wb"))) {
		fclose(in);
		return -1;
	}
	while ((n = fread(buf, 1, sizeof buf, in)) > 0)
		if (fwrite(buf, 1, n, out) != n) {
			fclose(in);
			fclose(out);
			return -1;
		}
	fclose(in);
	if (fclose(out) != 0)
		return -1;
	rec_add(R_ASSET, dst, "", "", "");
	return 0;
}

int
tm_symlink(const char *target, const char *path)
{
	struct stat st;

	if (!lstat(path, &st)) {
		if (!S_ISLNK(st.st_mode)) {
			fprintf(stderr, "themer: %s is in the way (not a link); leaving it alone\n", path);
			return -1;
		}
		unlink(path);
	}
	if (mkdirs_parent(path) < 0 || symlink(target, path) < 0)
		return -1;
	rec_add(R_LINK, path, "", "", "");
	return 0;
}

int
tm_write_asset(const char *path, const void *data, size_t len)
{
	FILE *f;

	if (!access(path, F_OK) && !is_ours(path) && !was_asset(path)) {
		fprintf(stderr, "themer: not overwriting %s (not placed by themer)\n", path);
		return -1;
	}
	if (mkdirs_parent(path) < 0 || !(f = fopen(path, "wb")))
		return -1;
	if (fwrite(data, 1, len, f) != len) {
		fclose(f);
		return -1;
	}
	if (fclose(f) != 0)
		return -1;
	rec_add(R_ASSET, path, "", "", "");
	return 0;
}

/* Copies every regular file under srcdir into dstdir, subdirectories included,
 * except the names in skip (NULL-terminated, top level only): the files the module
 * renders instead. Returns the number copied, or -1 on the first failure. */
int
tm_copy_tree(const char *srcdir, const char *dstdir, const char *const *skip)
{
	DIR *d = opendir(srcdir);
	struct dirent *e;
	int n = 0;

	if (!d)
		return 0;
	while ((e = readdir(d))) {
		char s[PATH_MAX], t[PATH_MAX];
		struct stat st;
		int skipped = 0;

		if (e->d_name[0] == '.')
			continue;
		for (int i = 0; skip && skip[i]; i++)
			if (!strcmp(skip[i], e->d_name))
				skipped = 1;
		if (skipped)
			continue;
		if (snprintf(s, sizeof s, "%s/%s", srcdir, e->d_name) >= (int)sizeof s ||
		    snprintf(t, sizeof t, "%s/%s", dstdir, e->d_name) >= (int)sizeof t || stat(s, &st))
			continue;
		if (S_ISDIR(st.st_mode)) {
			int k = tm_copy_tree(s, t, NULL);

			if (k < 0) {
				closedir(d);
				return -1;
			}
			n += k;
		} else if (S_ISREG(st.st_mode)) {
			if (tm_copy(s, t) < 0) {
				closedir(d);
				return -1;
			}
			n++;
		}
	}
	closedir(d);
	return n;
}

int
tm_replacef(const char *path, const char *fmt, ...)
{
	char bak[PATH_MAX];
	int exists, ours = 0, moved = 0;
	va_list ap;
	FILE *f;

	snprintf(bak, sizeof bak, "%s%s", path, BACKUP_SUFFIX);
	exists = !access(path, F_OK);
	ours = exists && is_ours(path);

	if (exists && !ours) {
		/* a user file: keep it aside, once; refuse to lose an earlier backup */
		if (!access(bak, F_OK)) {
			fprintf(stderr, "themer: %s exists; not replacing %s\n", bak, path);
			return -1;
		}
		if (mkdirs_parent(path) < 0 || rename(path, bak) < 0)
			return -1;
		moved = 1;
	}
	if (mkdirs_parent(path) < 0 || !(f = fopen(path, "w")))
		return -1;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	if (fclose(f) != 0)
		return -1;
	if (moved)
		rec_add(R_REPLACE, path, "", "", "");
	else if (!exists)
		rec_add(R_FILE, path, "", "", "");
	return 0;
}

/* Replaces the last line that starts with prefix (a whole word, such as "set $menu")
 * with text, and records the old line for unload. Returns 0 when replaced, 1 when
 * there is no such line, and -1 on error. */
int
tm_setline(const char *path, const char *prefix, const char *text)
{
	Lines L;
	char old[256];
	int at, r;

	if (lines_read(path, &L) < 0)
		return 1;
	at = find_setline(&L, prefix);
	if (at < 0) {
		lines_free(&L);
		return 1;
	}
	snprintf(old, sizeof old, "%s", L.v[at]);
	lines_del(&L, at);
	lines_ins(&L, at, text);
	r = lines_write(path, &L);
	lines_free(&L);
	if (r < 0)
		return -1;
	rec_add(R_LINE, path, prefix, "", old);
	return 0;
}

/* marker lines: "<prefix> themer:<id> begin<suffix>" and the matching end */
static void
block_marks(char *b, char *e, size_t n, const char *id, const char *prefix, const char *suffix)
{
	snprintf(b, n, "%s themer:%s begin%s", prefix, id, suffix);
	snprintf(e, n, "%s themer:%s end%s", prefix, id, suffix);
}

static int
block_find(const Lines *L, const char *b, const char *e, int *from, int *to)
{
	int i, j;

	for (i = 0; i < L->n; i++)
		if (!strcmp(L->v[i], b))
			break;
	if (i == L->n)
		return 0;
	for (j = i + 1; j < L->n; j++)
		if (!strcmp(L->v[j], e))
			break;
	if (j == L->n)
		return 0;
	*from = i;
	*to = j;
	return 1;
}

static void
block_remove_all(Lines *L, const char *b, const char *e)
{
	int from, to, k;

	while (block_find(L, b, e, &from, &to))
		for (k = from; k <= to; k++)
			lines_del(L, from);
}

int
tm_blockf(const char *path, const char *id, const char *prefix, const char *suffix,
          int top, const char *fmt, ...)
{
	static char body[MAX_BODY];
	char b[512], e[512], *s, *nl;
	Lines L;
	va_list ap;
	int existed, at, r;

	va_start(ap, fmt);
	vsnprintf(body, sizeof body, fmt, ap);
	va_end(ap);

	block_marks(b, e, sizeof b, id, prefix, suffix);
	existed = lines_read(path, &L) == 0;
	if (!existed && mkdirs_parent(path) < 0)
		return -1;
	block_remove_all(&L, b, e);

	at = top ? 0 : L.n;
	lines_ins(&L, at++, b);
	for (s = body; *s; s = nl + 1) {
		nl = strchr(s, '\n');
		if (nl)
			*nl = '\0';
		lines_ins(&L, at++, s);
		if (!nl)
			break;
	}
	lines_ins(&L, at, e);

	r = lines_write(path, &L);
	lines_free(&L);
	if (r < 0)
		return -1;
	if (!existed)
		rec_add(R_CREATED, path, "", "", "");
	rec_add(R_BLOCK, path, id, prefix, suffix);
	return 0;
}

/* Finds [start, end) of a section and the last line of key in it.
 * start = -1 when the section is missing. kline = -1 when the key is absent. */
static void
ini_locate(const Lines *L, const char *section, const char *key,
           int *start, int *end, int *kline)
{
	size_t klen = strlen(key);
	char hdr[NAME_LEN + 4];
	int i, at = -1;

	*start = -1;
	*kline = -1;
	if (section[0]) {
		snprintf(hdr, sizeof hdr, "[%s]", section);
		for (i = 0; i < L->n; i++)
			if (!strcmp(skip_ws(L->v[i]), hdr))
				at = i;
		if (at < 0) {
			*end = L->n;
			return;
		}
		*start = at + 1;
	} else {
		*start = 0;
	}
	for (*end = *start; *end < L->n && !is_header(L->v[*end]); (*end)++)
		;
	for (i = *end - 1; i >= *start; i--) {
		const char *t = skip_ws(L->v[i]);

		if (*t == '#' || *t == ';')
			continue;
		if (!strncmp(t, key, klen) && *skip_ws(t + klen) == '=') {
			*kline = i;
			break;
		}
	}
}

int
tm_ini(const char *path, const char *section, const char *key, const char *value)
{
	char old[256] = "", line[512], hdr[NAME_LEN + 4];
	int existed, start, end, k, at;
	Lines L;

	existed = lines_read(path, &L) == 0;
	if (!existed && mkdirs_parent(path) < 0)
		return -1;
	ini_locate(&L, section, key, &start, &end, &k);
	snprintf(line, sizeof line, "%s=%s", key, value);

	if (k >= 0) {
		const char *v = skip_ws(skip_ws(L.v[k]) + strlen(key)) + 1;

		snprintf(old, sizeof old, "%s", skip_ws(v));
		trim_right(old);
		lines_del(&L, k);
		lines_ins(&L, k, line);
	} else if (start < 0 && section[0]) {
		snprintf(old, sizeof old, "%s", NEW_SECTION);
		if (L.n && !blank(L.v[L.n - 1])) {
			lines_ins(&L, L.n, "");
			snprintf(old, sizeof old, "%s", NEW_SECTION_BLANK);
		}
		snprintf(hdr, sizeof hdr, "[%s]", section);
		lines_ins(&L, L.n, hdr);
		lines_ins(&L, L.n, line);
	} else {
		at = end;
		while (at > start && blank(L.v[at - 1]))
			at--;
		lines_ins(&L, at, line);
	}

	if (lines_write(path, &L) < 0) {
		lines_free(&L);
		return -1;
	}
	lines_free(&L);
	if (!existed)
		rec_add(R_CREATED, path, "", "", "");
	rec_add(R_KEY, path, section, key, old);
	return 0;
}

/* ---- undo ---- */

static int
file_empty(const char *path)
{
	Lines L;
	int empty = 1;

	if (lines_read(path, &L) < 0)
		return 0;
	for (int i = 0; i < L.n; i++)
		if (!blank(L.v[i]))
			empty = 0;
	lines_free(&L);
	return empty;
}

static void
undo_block(const Rec *r)
{
	char b[512], e[512];
	Lines L;

	if (lines_read(r->path, &L) < 0)
		return;
	block_marks(b, e, sizeof b, r->s[0], r->s[1], r->s[2]);
	block_remove_all(&L, b, e);
	lines_write(r->path, &L);
	lines_free(&L);
}

static void
undo_key(const Rec *r)
{
	char line[512];
	int start, end, k, i;
	Lines L;

	if (lines_read(r->path, &L) < 0)
		return;
	ini_locate(&L, r->s[0], r->s[1], &start, &end, &k);
	if (k >= 0) {
		lines_del(&L, k);
		if (!strcmp(r->s[2], NEW_SECTION) || !strcmp(r->s[2], NEW_SECTION_BLANK)) {
			/* drop the section we created if nothing else is left in it */
			ini_locate(&L, r->s[0], r->s[1], &start, &end, &k);
			if (start >= 0) {
				for (i = start; i < end && blank(L.v[i]); i++)
					;
				if (i == end) {
					for (i = end - 1; i >= start - 1; i--)
						lines_del(&L, i);
					if (!strcmp(r->s[2], NEW_SECTION_BLANK) && start - 2 >= 0 &&
					    blank(L.v[start - 2]))
						lines_del(&L, start - 2);
				}
			}
		} else if (r->s[2][0]) {
			snprintf(line, sizeof line, "%s=%s", r->s[1], r->s[2]);
			lines_ins(&L, k, line);
		}
	}
	lines_write(r->path, &L);
	lines_free(&L);
}

/* the last line of cfg that starts with prefix as a whole word, or -1 */
static int
find_setline(const Lines *L, const char *prefix)
{
	size_t plen = strlen(prefix);
	int i, at = -1;

	for (i = 0; i < L->n; i++) {
		const char *s = skip_ws(L->v[i]);

		if (!strncmp(s, prefix, plen) && (s[plen] == '\0' || s[plen] == ' ' || s[plen] == '\t'))
			at = i;
	}
	return at;
}

static void
undo_line(const Rec *r)
{
	Lines L;
	int at;

	if (lines_read(r->path, &L) < 0)
		return;
	at = find_setline(&L, r->s[0]);
	if (at >= 0 && r->s[2][0]) {
		lines_del(&L, at);
		lines_ins(&L, at, r->s[2]);
	}
	lines_write(r->path, &L);
	lines_free(&L);
}

static void
undo_all(void)
{
	for (int i = nrecs - 1; i >= 0; i--) {
		const Rec *r = &recs[i];

		switch (r->kind) {
		case R_FILE:
			/* only remove it if it is still ours: the user may have replaced it */
			if (is_ours(r->path))
				unlink(r->path);
			break;
		case R_CREATED:
			if (file_empty(r->path))
				unlink(r->path);
			break;
		case R_REPLACE: {
			char bak[PATH_MAX];

			snprintf(bak, sizeof bak, "%s%s", r->path, BACKUP_SUFFIX);
			if (!access(bak, F_OK)) {
				unlink(r->path);
				rename(bak, r->path);
			}
			break;
		}
		case R_ASSET: unlink(r->path); break;
		case R_LINK: {
			struct stat st;

			if (!lstat(r->path, &st) && S_ISLNK(st.st_mode))
				unlink(r->path);
			break;
		}
		case R_DIR:   rmdir(r->path); break;
		case R_BLOCK: undo_block(r); break;
		case R_KEY:   undo_key(r); break;
		case R_LINE:  undo_line(r); break;
		}
	}
}

/* removes everything the module recorded last time, then runs its unload hook */
static void
cleanup(const TmModule *m)
{
	char p[PATH_MAX];

	recording = 0;
	if (manifest_read(m->name) == 0) {
		undo_all();
		if (manifest_path(p, sizeof p, m->name) == 0)
			unlink(p);
	}
	nrecs = 0;
	if (m->unload)
		m->unload();
}

int
mod_unload(const TmModule *m)
{
	cleanup(m);
	return 0;
}

int
mod_load(const TmModule *m, const TmTheme *t)
{
	int r;

	cleanup(m);
	recording = 1;
	nrecs = 0;
	r = m->load(t);
	recording = 0;
	if (manifest_write(m->name) < 0)
		fprintf(stderr, "themer: could not write manifest for %s\n", m->name);
	nrecs = 0;
	return r ? -1 : 0;
}

/* ---- options and the loaded-module list ---- */

const char *
tm_opt(const TmTheme *t, const char *key)
{
	for (int i = 0; i < t->nopts; i++)
		if (!strcmp(t->opts[i].key, key))
			return t->opts[i].value;
	return NULL;
}

int
tm_loaded(const TmTheme *t, const char *module)
{
	for (int i = 0; i < t->nloaded; i++)
		if (!strcmp(t->loaded[i], module))
			return 1;
	return 0;
}

/* ---- executable files ---- */

int
tm_writex(const char *path, const char *fmt, ...)
{
	char body[16384];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(body, sizeof body, fmt, ap);
	va_end(ap);
	if (tm_writef(path, "%s", body) < 0)
		return -1;
	return chmod(path, 0755);
}

/* ---- fonts ---- */

void
tm_font_for(const TmTheme *t, const char *key, char *fam, size_t n, int *size)
{
	const char *which = tm_opt(t, key);

	if (which && !strcmp(which, "terminal")) {
		snprintf(fam, n, "%s", t->mono_font);
		*size = t->mono_size;
	} else {
		snprintf(fam, n, "%s", t->ui_font);
		*size = t->ui_size;
	}
}

