/* manifest.c: the tm_* helpers modules write files with, and the manifest that
 * makes every write undoable. Everything of themer's own lives under
 * ~/.config/themer: the manifest is undo/<name>.manifest there, and a user file a
 * module edits in place is first copied to originals/<name>/<its path> (record O,
 * with a hash of the file as the module left it). Unload puts that copy back byte
 * for byte when the file still has that hash, so a module never leaves a trace;
 * when the user has changed the file since, the records are undone one by one
 * instead, which takes out only themer's lines and keeps the user's edits.
 * Manifests and .themer-orig backups of earlier builds (~/.local/state/themer,
 * next to the file) are still read. The manifest is
 * a "#themer-manifest 2" line, then one record per line, fields separated by tabs;
 * tabs, newlines and backslashes inside a field are written \t, \n and \\ (a
 * replaced line can hold tabs). A manifest without the first line is from an
 * earlier build and is read as it is. Records have no size limit:
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
 *   M path                       blocks switched off by tm_disable_block: lines
 *                                starting with OFF_PREFIX get it taken away
 *   O path hash                  a user file edited in place: its original is in
 *                                originals/, hash is the file as the module left it
 */
#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "themer.h"

#define MANIFEST_V  "#themer-manifest 2"
#define MAX_BODY    (2 * 1024 * 1024)   /* a block: the userChrome stylesheet with its icons */
#define NEW_SECTION       "<new>"       /* section was created */
#define NEW_SECTION_BLANK "<new+blank>" /* ...and a blank line was added before it */

enum { R_FILE = 'F', R_CREATED = 'C', R_DIR = 'D', R_BLOCK = 'B', R_KEY = 'K', R_REPLACE = 'X', R_LINE = 'L', R_ASSET = 'A', R_LINK = 'S', R_ORIG = 'O', R_OFF = 'M', R_JSON = 'J' };

#define BACKUP_SUFFIX ".themer-orig"
#define OFF_PREFIX    "#themer-off: "

typedef struct {
	char kind;
	char *path;
	char *s[3];
} Rec;

typedef struct { char **v; int n, cap; } Lines;

static int find_setline(const Lines *L, const char *prefix);

static Rec *recs;
static int nrecs, caprecs, recording;
static char cur_mod[NAME_LEN];       /* the module being loaded or undone */

static char *
xstrdup(const char *s)
{
	char *d = strdup(s ? s : "");

	if (!d) {
		perror("themer");
		exit(1);
	}
	return d;
}

static void
recs_clear(void)
{
	for (int i = 0; i < nrecs; i++) {
		free(recs[i].path);
		for (int k = 0; k < 3; k++)
			free(recs[i].s[k]);
	}
	nrecs = 0;
}

static Rec *
rec_new(void)
{
	if (nrecs == caprecs) {
		int cap = caprecs ? 2 * caprecs : 256;
		Rec *r = realloc(recs, (size_t)cap * sizeof *r);

		if (!r) {
			perror("themer");
			exit(1);
		}
		recs = r;
		caprecs = cap;
	}
	return &recs[nrecs++];
}

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

	if (!recording)
		return;
	for (i = 0; i < nrecs; i++)
		if (recs[i].kind == kind && !strcmp(recs[i].path, path) &&
		    !strcmp(recs[i].s[0], a) && !strcmp(recs[i].s[1], b))
			return;
	r = rec_new();
	r->kind = kind;
	r->path = xstrdup(path);
	r->s[0] = xstrdup(a);
	r->s[1] = xstrdup(b);
	r->s[2] = xstrdup(c);
}

static int
manifest_path(char *p, size_t n, const char *mod)
{
	return snprintf(p, n, "%s/.config/themer/undo/%s.manifest", home(), mod) < (int)n ? 0 : -1;
}

static int mkdirs_parent(const char *path);

/* ---- directories themer made that were not empty when their maker went: another
 * module (or the host) had files in them. Kept in a list and tried again after
 * every unload, so nothing themer made is left behind once its last file goes. */

static int
pending_path(char *p, size_t n)
{
	return snprintf(p, n, "%s/.config/themer/undo/dirs.pending", home()) < (int)n ? 0 : -1;
}

void
dirs_note(const char *dir)
{
	char p[PATH_MAX], line[PATH_MAX];
	FILE *f;

	if (pending_path(p, sizeof p) < 0)
		return;
	if ((f = fopen(p, "r"))) {
		while (fgets(line, sizeof line, f)) {
			line[strcspn(line, "\n")] = '\0';
			if (!strcmp(line, dir)) {
				fclose(f);
				return;
			}
		}
		fclose(f);
	}
	mkdirs_parent(p);
	if ((f = fopen(p, "a"))) {
		fprintf(f, "%s\n", dir);
		fclose(f);
	}
}

static int
cmp_depth(const void *a, const void *b)
{
	return (int)strlen(*(char *const *)b) - (int)strlen(*(char *const *)a);
}

/* removes each listed directory that is empty now (the deepest first); the rest,
 * and those that are gone, are dropped from the list or kept as they are */
void
dirs_retry(void)
{
	char p[PATH_MAX], line[PATH_MAX], *dirs[512], **rest = NULL;
	int n = 0, kept = 0, rest_n = 0;
	FILE *f;

	if (pending_path(p, sizeof p) < 0 || !(f = fopen(p, "r")))
		return;
	while (n < 512 && fgets(line, sizeof line, f)) {
		line[strcspn(line, "\n")] = '\0';
		if (line[0])
			dirs[n++] = xstrdup(line);
	}
	/* more than fit: the rest are kept for the next time, untried */
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\n")] = '\0';
		if (line[0] && rest_n < 4096) {
			rest = realloc(rest, (size_t)(rest_n + 1) * sizeof *rest);
			rest[rest_n++] = xstrdup(line);
		}
	}
	fclose(f);
	qsort(dirs, n, sizeof dirs[0], cmp_depth);
	for (int i = 0; i < n; i++) {
		if (rmdir(dirs[i]) == 0 || errno == ENOENT) {
			free(dirs[i]);
			dirs[i] = NULL;
		} else {
			kept++;
		}
	}
	if (!kept && !rest_n) {
		unlink(p);
	} else if ((f = fopen(p, "w"))) {
		for (int i = 0; i < n; i++)
			if (dirs[i])
				fprintf(f, "%s\n", dirs[i]);
		for (int i = 0; i < rest_n; i++)
			fprintf(f, "%s\n", rest[i]);
		fclose(f);
	}
	for (int i = 0; i < n; i++)
		free(dirs[i]);
	for (int i = 0; i < rest_n; i++)
		free(rest[i]);
	free(rest);
}

/* where earlier builds kept it */
static int
legacy_manifest_path(char *p, size_t n, const char *mod)
{
	return snprintf(p, n, "%s/.local/state/themer/%s.manifest", home(), mod) < (int)n ? 0 : -1;
}

/* the copy of a user file kept before the module first edited it */
static int
original_path(char *p, size_t n, const char *mod, const char *path)
{
	return snprintf(p, n, "%s/.config/themer/originals/%s%s%s", home(), mod,
	                path[0] == '/' ? "" : "/", path) < (int)n ? 0 : -1;
}

/* FNV-1a over a file's bytes, as text; "" when it cannot be read */
static void
file_hash(const char *path, char *out, size_t n)
{
	unsigned long long h = 1469598103934665603ULL;
	unsigned char buf[8192];
	size_t k;
	FILE *f = fopen(path, "rb");

	out[0] = '\0';
	if (!f)
		return;
	while ((k = fread(buf, 1, sizeof buf, f)) > 0)
		for (size_t i = 0; i < k; i++)
			h = (h ^ buf[i]) * 1099511628211ULL;
	fclose(f);
	snprintf(out, n, "%016llx", h);
}

static int
copy_file(const char *src, const char *dst)
{
	unsigned char buf[65536];
	FILE *in = fopen(src, "rb"), *out;
	AFile a;
	size_t k;

	if (!in)
		return -1;
	if (!(out = afile_open(&a, dst, 1))) {
		fclose(in);
		return -1;
	}
	while ((k = fread(buf, 1, sizeof buf, in)) > 0)
		if (fwrite(buf, 1, k, out) != k)
			break;
	if (ferror(in) || k) {
		fclose(in);
		afile_abort(&a);
		return -1;
	}
	fclose(in);
	a.sync = 1;              /* a user's original, or a user file put back */
	return afile_commit(&a);
}

/* Before a module edits a user file in place: a copy of it as it is now, once per
 * load. The hash is filled in when the manifest is written. */
static void
snapshot(const char *path)
{
	char o[PATH_MAX];

	if (!recording || access(path, F_OK))
		return;
	/* only the file as it was before this load touched it: once the module has
	 * created or changed it, a copy would hold themer's own lines */
	for (int i = 0; i < nrecs; i++)
		if (!strcmp(recs[i].path, path))
			return;
	/* a copy already there that no manifest covers is from a load that never
	 * finished (themer killed, the manifest not written) or an undo that could not
	 * put it back: it is the file from before themer, which the file now is not */
	if (original_path(o, sizeof o, cur_mod, path) == 0 && !access(o, F_OK)) {
		rec_add(R_ORIG, path, "", "", "");
		return;
	}
	if (original_path(o, sizeof o, cur_mod, path) < 0 || copy_file(path, o) < 0) {
		fprintf(stderr, "themer: could not keep a copy of %s; unload will undo it line by line\n", path);
		return;
	}
	rec_add(R_ORIG, path, "", "", "");
}

/* one field, its tabs, newlines and backslashes escaped */
static void
put_field(FILE *f, const char *s)
{
	for (; *s; s++)
		switch (*s) {
		case '\t': fputs("\\t", f); break;
		case '\n': fputs("\\n", f); break;
		case '\r': fputs("\\r", f); break;
		case '\\': fputs("\\\\", f); break;
		default: fputc(*s, f);
		}
}

/* undoes put_field in place */
static void
unescape(char *s)
{
	char *o = s;

	for (; *s; s++) {
		if (*s == '\\' && s[1]) {
			s++;
			*o++ = *s == 't' ? '\t' : *s == 'n' ? '\n' : *s == 'r' ? '\r' : *s;
		} else {
			*o++ = *s;
		}
	}
	*o = '\0';
}

static int
manifest_write(const char *mod)
{
	char p[PATH_MAX];
	AFile a;
	FILE *f;
	int i;

	if (manifest_path(p, sizeof p, mod) < 0)
		return -1;
	if (!nrecs) {
		unlink(p);
		return 0;
	}
	/* the hash of each snapshotted file as the module left it */
	for (i = 0; i < nrecs; i++)
		if (recs[i].kind == R_ORIG) {
			char h[32];

			file_hash(recs[i].path, h, sizeof h);
			free(recs[i].s[0]);
			recs[i].s[0] = xstrdup(h);
		}
	if (!(f = afile_open(&a, p, 0)))
		return -1;
	fprintf(f, "%s\n", MANIFEST_V);
	for (i = 0; i < nrecs; i++) {
		fprintf(f, "%c\t", recs[i].kind);
		put_field(f, recs[i].path);
		for (int k = 0; k < 3; k++) {
			fputc('\t', f);
			put_field(f, recs[i].s[k]);
		}
		fputc('\n', f);
	}
	a.sync = 1;              /* unload depends on it */
	return afile_commit(&a);
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
	int escaped = 0, first = 1;

	if (manifest_path(p, sizeof p, mod) < 0)
		return -1;
	if (!(f = fopen(p, "r")) &&
	    (legacy_manifest_path(p, sizeof p, mod) < 0 || !(f = fopen(p, "r"))))
		return -1;
	recs_clear();
	while (getline(&line, &cap, f) > 0) {
		char *fld[5] = { 0 };
		int k, i;
		Rec *r;

		line[strcspn(line, "\r\n")] = '\0';
		if (first) {
			first = 0;
			if (!strcmp(line, MANIFEST_V)) {
				escaped = 1;
				continue;
			}
		}
		if (!line[0] || line[0] == '#')
			continue;
		k = split_tabs(line, fld, 5);
		for (i = k; i < 5; i++)
			fld[i] = (char *)empty;
		if (escaped)
			for (i = 1; i < k; i++)
				unescape(fld[i]);
		if (!fld[1][0])
			continue;
		r = rec_new();
		r->kind = fld[0][0];
		r->path = xstrdup(fld[1]);
		for (i = 0; i < 3; i++)
			r->s[i] = xstrdup(fld[i + 2]);
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
	AFile a;
	FILE *f;
	int i;

	if (!(f = afile_open(&a, path, 0)))
		return -1;
	for (i = 0; i < L->n; i++)
		fprintf(f, "%s\n", L->v[i]);
	a.sync = 1;              /* a user's file, edited in place */
	return afile_commit(&a);
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
tm_run(const char *const argv[])
{
	pid_t pid;
	int st;

	if (!argv || !argv[0])
		return -1;
	if ((pid = fork()) < 0)
		return -1;
	if (pid == 0) {
		int null = open("/dev/null", O_RDWR);

		if (null >= 0) {
			dup2(null, STDIN_FILENO);
			dup2(null, STDOUT_FILENO);
			dup2(null, STDERR_FILENO);
			if (null > STDERR_FILENO)
				close(null);
		}
		execvp(argv[0], (char *const *)argv);
		_exit(127);
	}
	while (waitpid(pid, &st, 0) < 0)
		if (errno != EINTR)
			return -1;
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

int
tm_signal(const char *name, int sig)
{
	DIR *d = opendir("/proc");
	struct dirent *e;
	uid_t me = getuid();
	int n = 0;

	if (!d)
		return 0;
	while ((e = readdir(d))) {
		char path[64], comm[32] = "";
		struct stat st;
		FILE *f;
		size_t len;

		if (e->d_name[0] < '0' || e->d_name[0] > '9')
			continue;
		snprintf(path, sizeof path, "/proc/%.20s", e->d_name);
		if (stat(path, &st) || st.st_uid != me)
			continue;
		snprintf(path, sizeof path, "/proc/%.20s/comm", e->d_name);
		if (!(f = fopen(path, "r")))
			continue;
		if (fgets(comm, sizeof comm, f)) {
			len = strcspn(comm, "\n");
			if (len == strlen(name) && !strncmp(comm, name, len) &&
			    !kill((pid_t)strtol(e->d_name, NULL, 10), sig))
				n++;
		}
		fclose(f);
	}
	closedir(d);
	return n;
}

int
tm_have_flatpak(const char *app_id)
{
	char p[PATH_MAX];

	if (snprintf(p, sizeof p, "/var/lib/flatpak/exports/bin/%s", app_id) < (int)sizeof p && !access(p, F_OK))
		return 1;
	return snprintf(p, sizeof p, "%s/.local/share/flatpak/exports/bin/%s", home(), app_id) < (int)sizeof p &&
	       !access(p, F_OK);
}

int
tm_have_lib(const char *soname)
{
	static const char *const dirs[] = {
		"/usr/lib", "/usr/lib64", "/usr/local/lib", "/lib", "/lib64",
		"/usr/lib/x86_64-linux-gnu", "/usr/lib/aarch64-linux-gnu", "/usr/lib/i386-linux-gnu",
		"/usr/lib/arm-linux-gnueabihf", "/usr/lib/riscv64-linux-gnu",
	};
	char p[PATH_MAX];

	for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++)
		if (snprintf(p, sizeof p, "%s/%s", dirs[i], soname) < (int)sizeof p && !access(p, F_OK))
			return 1;
	return 0;
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
	AFile a;
	FILE *f;

	if (!access(path, F_OK) && !is_ours(path)) {
		fprintf(stderr, "themer: not overwriting %s (not generated by themer)\n", path);
		return -1;
	}
	if (mkdirs_parent(path) < 0 || !(f = afile_open(&a, path, 0)))
		return -1;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	if (afile_commit(&a) < 0)
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
	AFile a;
	size_t n;

	if (!access(dst, F_OK) && !is_ours(dst) && !was_asset(dst)) {
		fprintf(stderr, "themer: not overwriting %s (not placed by themer)\n", dst);
		return -1;
	}
	if (!(in = fopen(src, "rb")))
		return -1;
	if (mkdirs_parent(dst) < 0 || !(out = afile_open(&a, dst, 1))) {
		fclose(in);
		return -1;
	}
	while ((n = fread(buf, 1, sizeof buf, in)) > 0)
		if (fwrite(buf, 1, n, out) != n) {
			fclose(in);
			afile_abort(&a);
			return -1;
		}
	if (ferror(in)) {
		fclose(in);
		afile_abort(&a);
		return -1;
	}
	fclose(in);
	if (afile_commit(&a) < 0)
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
	AFile a;
	FILE *f;

	if (!access(path, F_OK) && !is_ours(path) && !was_asset(path)) {
		fprintf(stderr, "themer: not overwriting %s (not placed by themer)\n", path);
		return -1;
	}
	if (mkdirs_parent(path) < 0 || !(f = afile_open(&a, path, 1)))
		return -1;
	if (fwrite(data, 1, len, f) != len) {
		afile_abort(&a);
		return -1;
	}
	if (afile_commit(&a) < 0)
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
	AFile a;
	FILE *f;

	exists = !access(path, F_OK);
	ours = exists && is_ours(path);

	if (exists && !ours) {
		/* a user file: kept in ~/.config/themer/originals, once; an earlier copy
		 * there is the user's real original and is not overwritten */
		if (original_path(bak, sizeof bak, cur_mod, path) < 0)
			return -1;
		if (!access(bak, F_OK)) {
			fprintf(stderr, "themer: %s exists; not replacing %s\n", bak, path);
			return -1;
		}
		if (mkdirs_parent(bak) < 0 || copy_file(path, bak) < 0)
			return -1;
		moved = 1;
	}
	if (mkdirs_parent(path) < 0 || !(f = afile_open(&a, path, 0))) {
		if (moved)
			unlink(bak);            /* the user's file was not touched */
		return -1;
	}
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	if (afile_commit(&a) < 0) {
		if (moved)
			unlink(bak);
		return -1;
	}
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
	char *old;
	int at, r;

	if (lines_read(path, &L) < 0)
		return 1;
	at = find_setline(&L, prefix);
	if (at < 0) {
		lines_free(&L);
		return 1;
	}
	snapshot(path);
	old = xstrdup(L.v[at]);         /* the whole line, whatever its length */
	lines_del(&L, at);
	lines_ins(&L, at, text);
	r = lines_write(path, &L);
	lines_free(&L);
	if (r == 0)
		rec_add(R_LINE, path, prefix, "", old);
	free(old);
	return r < 0 ? -1 : 0;
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

/* the block goes first (top 1), last, or after the last line starting with after */
static int
vblockf(const char *path, const char *id, const char *prefix, const char *suffix,
        int top, const char *after, const char *fmt, va_list ap)
{
	static char body[MAX_BODY];
	char b[512], e[512], *s, *nl;
	Lines L;
	int existed, at, r;

	vsnprintf(body, sizeof body, fmt, ap);
	block_marks(b, e, sizeof b, id, prefix, suffix);
	snapshot(path);
	existed = lines_read(path, &L) == 0;
	if (!existed && mkdirs_parent(path) < 0)
		return -1;
	block_remove_all(&L, b, e);

	at = top ? 0 : L.n;
	if (after && (r = find_setline(&L, after)) >= 0)
		at = r + 1;
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

int
tm_blockf(const char *path, const char *id, const char *prefix, const char *suffix,
          int top, const char *fmt, ...)
{
	va_list ap;
	int r;

	va_start(ap, fmt);
	r = vblockf(path, id, prefix, suffix, top, NULL, fmt, ap);
	va_end(ap);
	return r;
}

int
tm_blockf_after(const char *path, const char *id, const char *prefix, const char *suffix,
                const char *after, const char *fmt, ...)
{
	va_list ap;
	int r;

	va_start(ap, fmt);
	r = vblockf(path, id, prefix, suffix, 0, after, fmt, ap);
	va_end(ap);
	return r;
}

/* ---- a top-level key of a JSON object, for files that allow no comments ---- */

#define JSON_ABSENT "\001absent"

/* past a // or block comment at s (JSONC: VS Code's settings), or s itself */
static const char *
json_comment(const char *s)
{
	if (s[0] == '/' && s[1] == '/') {
		while (*s && *s != '\n')
			s++;
		return s;
	}
	if (s[0] == '/' && s[1] == '*') {
		const char *e = strstr(s + 2, "*/");

		return e ? e + 2 : s + strlen(s);
	}
	return s;
}

/* the end of the JSON value starting at s (past whitespace), or NULL */
static const char *
json_value_end(const char *s)
{
	int depth = 0, str = 0;

	for (; *s; s++) {
		if (str) {
			if (*s == '\\' && s[1])
				s++;
			else if (*s == '"')
				str = 0;
			if (!str && !depth)
				return s + 1;
			continue;
		}
		if (json_comment(s) != s) {
			if (!depth)
				return s;          /* a comment after a scalar */
			s = json_comment(s) - 1;
		} else if (*s == '"')
			str = 1;
		else if (*s == '{' || *s == '[')
			depth++;
		else if (*s == '}' || *s == ']') {
			if (!depth)
				return s;          /* a scalar ends at the object's end */
			if (!--depth)
				return s + 1;
		} else if (!depth && (*s == ',' || *s == '\n'))
			return s;
	}
	return s;
}

/* Where key's value lies in the top-level object of text: *vs..*ve, and *ks the start
 * of "key". 0 when found; -1 when the key is not there (*vs: just past the "{"); -2
 * when the text is not one themer can edit (*vs NULL). */
static int
json_find(const char *text, const char *key, const char **ks, const char **vs, const char **ve)
{
	const char *s = text, *open = NULL;
	int depth = 0, str = 0;
	size_t kl = strlen(key);

	for (; *s; s++) {
		if (str) {
			if (*s == '\\' && s[1])
				s++;
			else if (*s == '"')
				str = 0;
			continue;
		}
		if (json_comment(s) != s) {
			s = json_comment(s) - 1;
			continue;
		}
		if (*s == '"') {
			if (depth == 1 && !strncmp(s + 1, key, kl) && s[kl + 1] == '"') {
				const char *c = s + kl + 2;

				while (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r')
					c++;
				if (*c == ':') {
					*ks = s;
					c++;
					/* the value may start on a later line, after comments */
					for (;;) {
						if (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r')
							c++;
						else if (json_comment(c) != c)
							c = json_comment(c);
						else
							break;
					}
					*vs = c;
					*ve = json_value_end(c);
					while (*ve > c && ((*ve)[-1] == ' ' || (*ve)[-1] == '\t'))
						(*ve)--;
					if (*ve == c) {
						*vs = NULL;     /* no value: not JSON themer can edit */
						return -2;
					}
					return 0;
				}
			}
			str = 1;
		} else if (*s == '{' || *s == '[') {
			if (!depth && *s == '{' && !open)
				open = s + 1;
			depth++;
		} else if (*s == '}' || *s == ']') {
			depth--;
		}
	}
	*vs = open;
	return -1;
}

static char *
file_text(const char *path)
{
	FILE *f = fopen(path, "rb");
	char *t;
	long n;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	rewind(f);
	t = malloc((size_t)n + 1);
	if (t && fread(t, 1, (size_t)n, f) != (size_t)n) {
		free(t);
		t = NULL;
	}
	if (t)
		t[n] = '\0';
	fclose(f);
	return t;
}

static int
text_write(const char *path, const char *a, size_t an, const char *mid, const char *b)
{
	AFile af;
	FILE *f = afile_open(&af, path, 1);

	if (!f)
		return -1;
	fwrite(a, 1, an, f);
	fputs(mid, f);
	fputs(b, f);
	return afile_commit(&af);
}

/* sets key to value (JSON text: "\"Themer\"", true, 12) in the top-level object;
 * old is the value it had, or JSON_ABSENT when there was none (then it is added) */
/* value as it goes in: no whitespace at its ends, and the lines after its first
 * indented by ind more, so a multi-line object sits under its key */
static char *
json_shape(const char *value, int ind)
{
	size_t n = strlen(value), o = 0;
	char *out;
	const char *s = value, *e = value + n;

	while (s < e && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'))
		s++;
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r'))
		e--;
	out = malloc((size_t)(e - s) * (size_t)(ind + 2) + 1);
	for (; s < e; s++) {
		out[o++] = *s;
		if (*s == '\n')
			for (int k = 0; k < ind; k++)
				out[o++] = ' ';
	}
	out[o] = '\0';
	return out;
}

static int
json_put(const char *path, const char *key, const char *value, char **old, int shape)
{
	char *t = file_text(path), *mid, *shaped = NULL;
	const char *ks, *vs, *ve;
	int r;

	if (!t)
		return -1;
	if (json_find(t, key, &ks, &vs, &ve) == 0) {
		const char *ls = ks;

		while (ls > t && ls[-1] != '\n')
			ls--;
		if (old)
			*old = strndup(vs, (size_t)(ve - vs));
		/* a value put back on unload goes back exactly as it was */
		shaped = shape ? json_shape(value, (int)(ks - ls)) : strdup(value);
		r = text_write(path, t, (size_t)(vs - t), shaped, ve);
	} else if (vs) {
		const char *c = vs;
		int empty, multi;
		size_t n;

		while (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r')
			c++;
		empty = *c == '}';
		/* an object on one line ({} or {"a": 1}) stays on one */
		multi = memchr(vs, '\n', (size_t)(c - vs)) != NULL;
		n = strlen(key) + strlen(value) + 16;
		mid = malloc(n);
		{
			/* indented as the first key is */
			const char *nl = memchr(vs, '\n', (size_t)(c - vs) + 1), *ind = nl ? nl + 1 : NULL;
			int w = 0;

			while (ind && (ind[w] == ' ' || ind[w] == '\t') && w < 16)
				w++;
			shaped = json_shape(value, multi ? (w ? w : 2) : 0);
			n = strlen(key) + strlen(shaped) + 40;
			mid = realloc(mid, n);
			snprintf(mid, n, "%s%.*s\"%s\": %s%s", multi ? "\n" : "", multi ? (w ? w : 2) : 0,
			         ind && w ? ind : "                ", key, shaped, empty ? "" : multi ? "," : ", ");
		}
		if (old)
			*old = strdup(JSON_ABSENT);
		r = text_write(path, t, (size_t)(vs - t), mid, vs);
		free(mid);
	} else {
		r = -1;               /* not an object */
	}
	free(shaped);
	free(t);
	return r;
}

/* takes key out again (what json_put added), with the comma that went with it */
static void
json_drop(const char *path, const char *key)
{
	char *t = file_text(path);
	const char *ks, *vs, *ve, *a, *b;

	if (!t)
		return;
	if (json_find(t, key, &ks, &vs, &ve) == 0) {
		const char *c2 = ve;

		a = ks;
		b = ve;
		while (*c2 == ' ' || *c2 == '\t' || *c2 == '\n' || *c2 == '\r')
			c2++;
		if (*c2 == ',') {
			b = c2 + 1;
			if (*b == ' ' && b[-1] == ',' && c2 == ve)
				b++;        /* the ", " of a one-line object */
		} else {                     /* the last key: the comma before it goes */
			const char *c = a;

			while (c > t && (c[-1] == ' ' || c[-1] == '\t' || c[-1] == '\n' || c[-1] == '\r'))
				c--;
			if (c > t && c[-1] == ',')
				a = c - 1;
		}
		/* the line it was on, when it had one of its own */
		while (a > t && (a[-1] == ' ' || a[-1] == '\t'))
			a--;
		if (a > t && a[-1] == '\n' && (*b == '\n' || *b == '\r'))
			a--;
		text_write(path, t, (size_t)(a - t), "", b);
	}
	free(t);
}

int
tm_json_set(const char *path, const char *key, const char *value)
{
	char *old = NULL;
	int r;

	snapshot(path);
	r = json_put(path, key, value, &old, 1);
	if (r == 0)
		rec_add(R_JSON, path, key, value, old);
	free(old);
	return r;
}

static void
undo_json(const Rec *r)
{
	if (!strcmp(r->s[2], JSON_ABSENT))
		json_drop(r->path, r->s[0]);
	else
		json_put(r->path, r->s[0], r->s[2], NULL, 0);
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

	snapshot(path);
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

/* the keyword at the start of a line, followed by space, tab or { */
static int
starts_block(const char *line, const char *kw)
{
	const char *p = skip_ws(line);
	size_t n = strlen(kw);

	return !strncmp(p, kw, n) && (p[n] == ' ' || p[n] == '\t' || p[n] == '{');
}

int
tm_disable_block(const char *path, const char *keyword)
{
	Lines L;
	int n = 0, r;

	if (lines_read(path, &L) < 0)
		return 0;
	for (int i = 0; i < L.n; i++) {
		int depth = 0, opened = 0;

		if (!starts_block(L.v[i], keyword))
			continue;
		snapshot(path);
		/* the block runs to the brace that closes its first { */
		for (int k = i; k < L.n; k++) {
			char *line = malloc(strlen(L.v[k]) + sizeof OFF_PREFIX);

			if (!line) {
				lines_free(&L);
				return -1;
			}
			/* a comment is a whole line (sway, i3); # inside a line is a color */
			for (const char *c = *skip_ws(L.v[k]) == '#' ? "" : L.v[k]; *c; c++)
				if (*c == '{')
					depth++, opened = 1;
				else if (*c == '}')
					depth--;
			sprintf(line, "%s%s", OFF_PREFIX, L.v[k]);
			free(L.v[k]);
			L.v[k] = line;
			i = k;
			if (opened && depth <= 0)
				break;
		}
		n++;
	}
	if (!n) {
		lines_free(&L);
		return 0;
	}
	r = lines_write(path, &L);
	lines_free(&L);
	if (r < 0)
		return -1;
	rec_add(R_OFF, path, keyword, "", "");
	return n;
}

static void
undo_off(const Rec *r)
{
	Lines L;
	size_t n = strlen(OFF_PREFIX);

	if (lines_read(r->path, &L) < 0)
		return;
	for (int i = 0; i < L.n; i++)
		if (!strncmp(L.v[i], OFF_PREFIX, n))
			memmove(L.v[i], L.v[i] + n, strlen(L.v[i] + n) + 1);
	lines_write(r->path, &L);
	lines_free(&L);
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

/* removes the directories a copy in originals/ leaves empty, up to originals/ */
static void
prune_originals(const char *copy)
{
	char d[PATH_MAX], root[PATH_MAX];
	char *slash;

	snprintf(root, sizeof root, "%s/.config/themer/originals", home());
	snprintf(d, sizeof d, "%s", copy);
	while ((slash = strrchr(d, '/')) && slash > d) {
		*slash = '\0';
		if (strlen(d) <= strlen(root) || rmdir(d) < 0)
			break;
	}
	rmdir(root);
}

/* true if an O record put this file back whole, so its other records are done */
static int
restored(const char *path)
{
	for (int i = 0; i < nrecs; i++)
		if (recs[i].kind == R_ORIG && !strcmp(recs[i].path, path) && !strcmp(recs[i].s[1], "restored"))
			return 1;
	return 0;
}

static void
undo_all(void)
{
	/* first, every edited file the user has not touched since: its original back,
	 * byte for byte */
	for (int i = 0; i < nrecs; i++) {
		Rec *r = &recs[i];
		char o[PATH_MAX], h[32];

		if (r->kind != R_ORIG || original_path(o, sizeof o, cur_mod, r->path) < 0 || access(o, R_OK))
			continue;
		file_hash(r->path, h, sizeof h);
		if (r->s[0][0] && !strcmp(h, r->s[0])) {
			if (copy_file(o, r->path) < 0) {
				/* kept: the next load uses it as the original (see snapshot) */
				fprintf(stderr, "themer: could not put %s back from its copy; undoing line by line\n", r->path);
				continue;
			}
			free(r->s[1]);
			r->s[1] = xstrdup("restored");
		}
		unlink(o);
		prune_originals(o);
	}
	for (int i = nrecs - 1; i >= 0; i--) {
		const Rec *r = &recs[i];

		if (r->kind != R_FILE && r->kind != R_ASSET && r->kind != R_DIR && r->kind != R_LINK &&
		    restored(r->path))
			continue;
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

			if (original_path(bak, sizeof bak, cur_mod, r->path) == 0 && !access(bak, F_OK)) {
				if (copy_file(bak, r->path) == 0) {
					unlink(bak);
					prune_originals(bak);
				}
				break;
			}
			snprintf(bak, sizeof bak, "%s%s", r->path, BACKUP_SUFFIX);   /* earlier builds */
			if (!access(bak, F_OK)) {
				unlink(r->path);
				rename(bak, r->path);
			}
			break;
		}
		case R_ORIG:
			break;              /* handled first, in undo_all */
		case R_ASSET: unlink(r->path); break;
		case R_LINK: {
			struct stat st;

			if (!lstat(r->path, &st) && S_ISLNK(st.st_mode))
				unlink(r->path);
			break;
		}
		case R_DIR:
			/* still holding another module's files: kept in mind, removed once empty */
			if (rmdir(r->path) < 0 && errno == ENOTEMPTY)
				dirs_note(r->path);
			break;
		case R_BLOCK: undo_block(r); break;
		case R_KEY:   undo_key(r); break;
		case R_LINE:  undo_line(r); break;
		case R_JSON:  undo_json(r); break;
		case R_OFF:   undo_off(r); break;
		}
	}
}

/* removes everything the module recorded last time, then runs its unload hook */
static void
cleanup(const TmModule *m)
{
	char p[PATH_MAX];

	recording = 0;
	snprintf(cur_mod, sizeof cur_mod, "%s", m->name);
	if (manifest_read(m->name) == 0) {
		undo_all();
		dirs_retry();
		if (manifest_path(p, sizeof p, m->name) == 0)
			unlink(p);
		if (legacy_manifest_path(p, sizeof p, m->name) == 0)
			unlink(p);
	}
	recs_clear();
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
	snprintf(cur_mod, sizeof cur_mod, "%s", m->name);
	recording = 1;
	recs_clear();
	r = m->load(t);
	recording = 0;
	if (manifest_write(m->name) < 0)
		fprintf(stderr, "themer: could not write manifest for %s\n", m->name);
	recs_clear();
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

