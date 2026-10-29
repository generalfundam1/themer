/* template.c: template sets. A set is one directory, templates/<set>/<dir>/<file>:
 * <set> is its name (an identifier: themer_default, mine), <dir> the module's
 * template directory (its TmModule.templates; modules can share one), <file> one
 * output. A module picks a set with its "template" option and renders each of its
 * files with tm_render, which replaces {{name}} placeholders with values: the
 * module's own variables first, then the shared ones from palette.c. An unknown
 * {{name}} is an error, so a typo shows up instead of landing in the output.
 *
 * Lookup: ~/.config/themer/templates, then $THEMER_TEMPLATES (a source tree, in
 * place of the system directory), then SYS_TEMPLATES_DIR (replaced by "make install"). The built-in sets are
 * edited in ~/.config/themer/templates like any other: template_sync (run at every
 * start) mirrors them there and keeps the mirror current. It remembers what it
 * wrote in .seeded, as a hash per file, so it can tell a file you changed from one
 * you did not: an unchanged file follows the built-in when that changes; a changed
 * one is yours and is kept, with a note when the built-in moved on. A mirrored file
 * whose built-in disappears is removed again unless you changed it. */
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "themer.h"

#define SRC_MAX   (1024 * 1024)  /* largest template file read */
#define SEEDED    ".seeded"      /* in the user's template directory: path TAB hash lines */
#define MAX_ROOTS 3

typedef struct { char dir[MAX_ROOTS][PATH_MAX]; int n, user; } Roots;

/* The template roots, most specific first: the user's directory, $THEMER_TEMPLATES
 * (a source tree standing in for the system directory), the system directory. user
 * is the index of the user's, or -1 without HOME. */
static void
roots(Roots *r)
{
	const char *env = getenv("THEMER_TEMPLATES");

	r->n = 0;
	r->user = -1;
	if (path_under_home(r->dir[r->n], PATH_MAX, ".config/themer/templates") == 0)
		r->user = r->n++;
	if (env)
		snprintf(r->dir[r->n++], PATH_MAX, "%s", env);
	snprintf(r->dir[r->n++], PATH_MAX, "%s", SYS_TEMPLATES_DIR);
}

/* a set name: an identifier of letters, digits, '-' and '_', not hidden */
static int
valid_set(const char *s)
{
	if (!*s || *s == '.')
		return 0;
	for (; *s; s++)
		if (!isalnum((unsigned char)*s) && !strchr("-_", *s))
			return 0;
	return 1;
}

/* No set is the default one. Earlier builds named Themer Chicago, then the only
 * built-in set, "Themer Chicago" (and before that "default") and had a "Themer
 * Chicago Mask" set, now the firefox module's style=mask option; state files and
 * module options may still say so. */
static const char *
set_alias(const char *set)
{
	if (!set || !*set)
		return TM_DEFAULT_SET;
	if (!strcmp(set, "default") || !strcmp(set, "Themer Chicago") ||
	    !strcmp(set, "Themer Chicago Mask") || !strcmp(set, "themer_chicago_mask"))
		return "themer_chicago";
	return set;
}

/* a file name in a template set: letters, digits, '-', '_', '.', not hidden */
static int
valid_file(const char *s)
{
	if (!*s || *s == '.')
		return 0;
	for (; *s; s++)
		if (!isalnum((unsigned char)*s) && !strchr("-_.", *s))
			return 0;
	return 1;
}

/* out = root/set[/dir[/file]], or -1 if that does not fit */
static int
join(char *out, size_t n, const char *root, const char *set, const char *dir, const char *file)
{
	int len = file ? snprintf(out, n, "%s/%s/%s/%s", root, set, dir, file)
	        : dir  ? snprintf(out, n, "%s/%s/%s", root, set, dir)
	               : snprintf(out, n, "%s/%s", root, set);

	return len < 0 || (size_t)len >= n ? -1 : 0;
}

/* 1 if set is a built-in set (one the system directory, or $THEMER_TEMPLATES,
 * has): its name is taken, so a copy or an import needs another */
int
template_builtin(const char *set)
{
	char p[PATH_MAX];
	Roots r;

	if (!set || !valid_set(set))
		return 0;
	roots(&r);
	for (int i = r.user + 1; i < r.n; i++)
		if (join(p, sizeof p, r.dir[i], set, NULL, NULL) == 0 && !access(p, F_OK))
			return 1;
	return 0;
}

/* Finds <set>/<dir>/<file> in the first root that has it. */
static int
find_template(const char *dir, const char *set, const char *file, char *out, size_t n)
{
	Roots r;

	if (!valid_name(dir) || !valid_set(set) || !valid_file(file))
		return -1;
	roots(&r);
	for (int i = 0; i < r.n; i++)
		if (join(out, n, r.dir[i], set, dir, file) == 0 && !access(out, R_OK))
			return 0;
	return -1;
}

int
tm_set_info(const char *set, const char *key, char *out, size_t n)
{
	return set_info(set_alias(set), key, out, n);
}

int
tm_template_dir(const char *dir, const char *set, char *out, size_t n)
{
	Roots r;

	set = set_alias(set);
	if (!valid_name(dir) || !valid_set(set))
		return -1;
	roots(&r);
	for (int i = 0; i < r.n; i++)
		if (join(out, n, r.dir[i], set, dir, NULL) == 0 && !access(out, R_OK))
			return 0;
	return -1;
}

/* appends len bytes of s to out, failing if they would not fit */
static int
put(char *out, size_t n, size_t *o, const char *s, size_t len)
{
	if (*o + len >= n) {
		fprintf(stderr, "themer: template output too large\n");
		return -1;
	}
	memcpy(out + *o, s, len);
	*o += len;
	out[*o] = '\0';
	return 0;
}

static const char *
lookup(const TmVar *v, int n, const char *key)
{
	for (int i = 0; i < n; i++)
		if (!strcmp(v[i].name, key))
			return v[i].value;
	return NULL;
}

int
tm_render(char *out, size_t n, const char *dir, const char *set, const char *file,
          const TmTheme *t, const TmVar *extra, int nextra)
{
	static char src[SRC_MAX];
	char path[PATH_MAX];
	SharedStore st;
	TmVar shared[MAX_SHARED];
	const char *p;
	FILE *f;
	size_t len, o = 0;
	int nshared;

	set = set_alias(set);
	/* A set need not cover every module: what it lacks comes from the built-in set,
	 * so a set that restyles only Firefox still applies to the whole desktop. */
	if (find_template(dir, set, file, path, sizeof path) < 0 && strcmp(set, TM_DEFAULT_SET)) {
		if (find_template(dir, TM_DEFAULT_SET, file, path, sizeof path) == 0)
			set = TM_DEFAULT_SET;
	}
	if (find_template(dir, set, file, path, sizeof path) < 0) {
		fprintf(stderr, "themer: no template %s/%s/%s\n", dir, set, file);
		return -1;
	}
	if (!(f = fopen(path, "r")))
		return -1;
	len = fread(src, 1, sizeof src - 1, f);
	fclose(f);
	if (len == sizeof src - 1) {
		fprintf(stderr, "themer: template %s is larger than %d bytes\n", path, SRC_MAX - 1);
		return -1;
	}
	src[len] = '\0';
	out[0] = '\0';
	nshared = shared_vars(t, &st, shared, MAX_SHARED);

	for (p = src; *p;) {
		const char *open = strstr(p, "{{"), *end, *val;
		char key[64];
		size_t kl;

		if (!open)
			return put(out, n, &o, p, strlen(p));
		if (put(out, n, &o, p, (size_t)(open - p)) < 0)
			return -1;
		end = strstr(open + 2, "}}");
		if (!end || (kl = (size_t)(end - (open + 2))) >= sizeof key) {
			fprintf(stderr, "themer: %s/%s/%s: unterminated {{ placeholder\n", dir, set, file);
			return -1;
		}
		memcpy(key, open + 2, kl);
		key[kl] = '\0';
		val = lookup(extra, nextra, key);
		if (!val)
			val = lookup(shared, nshared, key);
		if (!val) {
			fprintf(stderr, "themer: %s/%s/%s: unknown variable {{%s}}\n", dir, set, file, key);
			return -1;
		}
		if (put(out, n, &o, val, strlen(val)) < 0)
			return -1;
		p = end + 2;
	}
	return 0;
}

/* ---- listing ---- */

static int
cmp_str(const void *a, const void *b)
{
	return strcmp(a, b);
}

/* The entries of <set>[/<dir>] across every root that are of the wanted type and
 * pass ok; set NULL lists the roots themselves. Names are listed once, sorted. */
static int
collect(const char *set, const char *dir, unsigned char want, int (*ok)(const char *),
        char names[][NAME_LEN], int max)
{
	Roots r;
	int n = 0;

	roots(&r);
	for (int i = 0; i < r.n; i++) {
		char path[PATH_MAX];
		DIR *d;
		struct dirent *e;

		if (set ? join(path, sizeof path, r.dir[i], set, dir, NULL) < 0
		        : snprintf(path, sizeof path, "%s", r.dir[i]) >= (int)sizeof path)
			continue;
		if (!(d = opendir(path)))
			continue;
		while ((e = readdir(d)) && n < max) {
			int dup = 0;

			if (!ok(e->d_name) || strlen(e->d_name) >= NAME_LEN)
				continue;
			if (e->d_type != want && e->d_type != DT_LNK && e->d_type != DT_UNKNOWN)
				continue;
			for (int k = 0; k < n; k++)
				if (!strcmp(names[k], e->d_name))
					dup = 1;
			if (!dup)
				snprintf(names[n++], NAME_LEN, "%.*s", NAME_LEN - 1, e->d_name);
		}
		closedir(d);
	}
	qsort(names, n, NAME_LEN, cmp_str);
	return n;
}

/* every set, sorted, listed once */
int
template_all_sets(char names[][NAME_LEN], int max)
{
	return collect(NULL, NULL, DT_DIR, valid_set, names, max);
}

/* the sets that cover a template directory */
int
template_sets(const char *dir, char names[][NAME_LEN], int max)
{
	char all[MAX_NAMES][NAME_LEN];
	int na = template_all_sets(all, MAX_NAMES), n = 0;

	if (!valid_name(dir))
		return 0;
	for (int i = 0; i < na && n < max; i++)
		if (template_has(dir, all[i]))
			snprintf(names[n++], NAME_LEN, "%s", all[i]);
	return n;
}

/* the files a set has for one template directory */
int
template_files(const char *dir, const char *set, char names[][NAME_LEN], int max)
{
	set = set_alias(set);
	if (!valid_name(dir) || !valid_set(set))
		return 0;
	return collect(set, dir, DT_REG, valid_file, names, max);
}

int
tm_template_names(const char *dir, char (*names)[TM_NAME_LEN], int max)
{
	char sets[MAX_NAMES][NAME_LEN], files[MAX_NAMES][NAME_LEN];
	int ns = template_all_sets(sets, MAX_NAMES), n = 0;

	if (!valid_name(dir))
		return 0;
	for (int i = 0; i < ns; i++) {
		int nf = template_files(dir, sets[i], files, MAX_NAMES);

		for (int k = 0; k < nf && n < max; k++) {
			int dup = 0;

			for (int j = 0; j < n && !dup; j++)
				dup = !strcmp(names[j], files[k]);
			if (!dup)
				snprintf(names[n++], TM_NAME_LEN, "%s", files[k]);
		}
	}
	qsort(names, n, TM_NAME_LEN, cmp_str);
	return n;
}

/* the template directories (one per module, or shared: wm, launcher) of every set */
int
template_dirs(char names[][NAME_LEN], int max)
{
	char all[MAX_NAMES][NAME_LEN], dirs[MAX_NAMES][NAME_LEN];
	int na = template_all_sets(all, MAX_NAMES), n = 0;

	for (int i = 0; i < na; i++) {
		int nd = collect(all[i], NULL, DT_DIR, valid_name, dirs, MAX_NAMES);

		for (int k = 0; k < nd && n < max; k++) {
			int dup = 0;

			for (int j = 0; j < n; j++)
				if (!strcmp(names[j], dirs[k]))
					dup = 1;
			if (!dup)
				snprintf(names[n++], NAME_LEN, "%s", dirs[k]);
		}
	}
	qsort(names, n, NAME_LEN, cmp_str);
	return n;
}

/* true if the set has any file in the directory */
int
template_has(const char *dir, const char *set)
{
	char files[MAX_NAMES][NAME_LEN];

	return template_files(dir, set, files, MAX_NAMES) > 0;
}

/* Copies every file of <from>/<dir> into the user's directory as <to>/<dir>. Returns
 * the number of files copied, or -1 on error; 0 means the source set has no files
 * there. An existing file in <to> is not overwritten. */
int
template_copy(const char *dir, const char *from, const char *to)
{
	static char buf[SRC_MAX];
	char files[MAX_NAMES][NAME_LEN], src[PATH_MAX], dst[PATH_MAX], user[PATH_MAX];
	int n, copied = 0;

	from = set_alias(from);
	if (!valid_name(dir) || !valid_set(from) || !valid_set(to) ||
	    path_under_home(user, sizeof user, ".config/themer/templates") < 0)
		return -1;
	n = template_files(dir, from, files, MAX_NAMES);
	for (int i = 0; i < n; i++) {
		FILE *f;
		size_t len;

		if (find_template(dir, from, files[i], src, sizeof src) < 0 ||
		    join(dst, sizeof dst, user, to, dir, files[i]) < 0)
			return -1;
		if (!access(dst, F_OK))
			continue;
		if (!(f = fopen(src, "rb")))
			return -1;
		len = fread(buf, 1, sizeof buf, f);
		fclose(f);
		if (len == sizeof buf || write_bytes(dst, buf, len) < 0)
			return -1;
		copied++;
	}
	return copied;
}

/* ---- the built-in sets, mirrored into the user's directory ---- */

/* both files' size and modification time, "" if the user's copy is missing */
static void
pair_stamp(const char *src, const char *dst, char *out, size_t n)
{
	struct stat a, b;

	out[0] = '\0';
	if (!stat(src, &a) && !stat(dst, &b))
		snprintf(out, n, "%lld.%lld.%ld:%lld.%lld.%ld", (long long)a.st_size, (long long)a.st_mtim.tv_sec,
		         a.st_mtim.tv_nsec, (long long)b.st_size, (long long)b.st_mtim.tv_sec, b.st_mtim.tv_nsec);
}

/* FNV-1a over a file's bytes, as 16 hex digits; "" if it cannot be read */
static void
file_hash(const char *path, char *out, size_t n)
{
	unsigned long long h = 1469598103934665603ULL;
	unsigned char buf[8192];
	size_t len;
	FILE *f = fopen(path, "rb");

	out[0] = '\0';
	if (!f)
		return;
	while ((len = fread(buf, 1, sizeof buf, f)) > 0)
		for (size_t i = 0; i < len; i++)
			h = (h ^ buf[i]) * 1099511628211ULL;
	fclose(f);
	snprintf(out, n, "%016llx", h);
}

/* the .seeded record: one "set/dir/file<TAB>hash<TAB>stamp" line per file themer
 * wrote. The stamp is both files' size and time when last compared: while neither
 * changed, neither is read again, so a start does not hash every built-in. */
typedef struct { char rel[256]; char hash[17]; char stamp[96]; int seen; } Seeded;
#define MAX_SEEDED 512

static int
seeded_load(const char *user, Seeded *rec, int max)
{
	char p[PATH_MAX], line[512];
	FILE *f;
	int n = 0;

	if (snprintf(p, sizeof p, "%s/%s", user, SEEDED) >= (int)sizeof p || !(f = fopen(p, "r")))
		return 0;
	while (n < max && fgets(line, sizeof line, f)) {
		char *tab = strchr(line, '\t');

		if (!tab)
			continue;
		*tab = '\0';
		snprintf(rec[n].rel, sizeof rec[n].rel, "%.255s", line);
		snprintf(rec[n].hash, sizeof rec[n].hash, "%.16s", tab + 1);
		rec[n].stamp[0] = '\0';
		if ((tab = strchr(tab + 1, '\t')))
			snprintf(rec[n].stamp, sizeof rec[n].stamp, "%.*s", (int)strcspn(tab + 1, "\n"), tab + 1);
		n++;
	}
	fclose(f);
	return n;
}

static int
seeded_save(const char *user, const Seeded *rec, int n)
{
	char p[PATH_MAX];
	FILE *f;

	if (snprintf(p, sizeof p, "%s/%s", user, SEEDED) >= (int)sizeof p || mkdirs(user) < 0 ||
	    !(f = fopen(p, "w")))
		return -1;
	fprintf(f, "# generated by themer: what it last wrote here, so it can tell your edits apart\n");
	for (int i = 0; i < n; i++)
		fprintf(f, "%s\t%s\t%s\n", rec[i].rel, rec[i].hash, rec[i].stamp);
	return fclose(f);
}

static Seeded *
seeded_find(Seeded *rec, int n, const char *rel)
{
	for (int i = 0; i < n; i++)
		if (!strcmp(rec[i].rel, rel))
			return &rec[i];
	return NULL;
}

static int
copy_file(const char *src, const char *dst)
{
	static char buf[SRC_MAX];
	FILE *f = fopen(src, "rb");
	size_t len;

	if (!f)
		return -1;
	len = fread(buf, 1, sizeof buf, f);
	fclose(f);
	return len == sizeof buf ? -1 : write_bytes(dst, buf, len);
}

/* One built-in file. missing in the user's directory: copied. Present and unchanged
 * since themer wrote it: follows the built-in. Changed by the user: kept; if the
 * built-in changed too, say so once. A file with no record (left by an old build)
 * is treated as themer's, so a stale copy cannot shadow the current one; a copy that
 * differs is kept beside it as <file>.themer-old. */
static int seeded_dirty;   /* the record changed this start: write it */

static int
sync_file(const char *user, const char *sys, const char *set, const char *dir, const char *file,
          Seeded *rec, int *nrec)
{
	char rel[256], src[PATH_MAX], dst[PATH_MAX], hsys[17], huser[17];
	Seeded *r;

	/* dir "" is a file at the set's root (set.conf) */
	if (dir[0]) {
		if (snprintf(rel, sizeof rel, "%s/%s/%s", set, dir, file) >= (int)sizeof rel ||
		    join(src, sizeof src, sys, set, dir, file) < 0 || join(dst, sizeof dst, user, set, dir, file) < 0)
			return -1;
	} else {
		if (snprintf(rel, sizeof rel, "%s/%s", set, file) >= (int)sizeof rel ||
		    join(src, sizeof src, sys, set, file, NULL) < 0 || join(dst, sizeof dst, user, set, file, NULL) < 0)
			return -1;
	}
	r = seeded_find(rec, *nrec, rel);
	if (r) {
		char now[96];

		r->seen = 1;
		pair_stamp(src, dst, now, sizeof now);
		if (now[0] && !strcmp(now, r->stamp))
			return 0;   /* neither changed since they were last compared */
	}
	seeded_dirty = 1;
	file_hash(src, hsys, sizeof hsys);
	file_hash(dst, huser, sizeof huser);
	if (!r) {
		if (*nrec == MAX_SEEDED)
			return -1;
		r = &rec[(*nrec)++];
		snprintf(r->rel, sizeof r->rel, "%s", rel);
		r->hash[0] = '\0';
		r->seen = 1;
		if (huser[0] && strcmp(huser, hsys)) {
			char old[PATH_MAX];

			if (snprintf(old, sizeof old, "%s.themer-old", dst) < (int)sizeof old)
				rename(dst, old);
			fprintf(stderr, "themer: %s: an old copy; replaced with the current built-in, "
			        "the old one kept as %s.themer-old\n", dst, file);
			huser[0] = '\0';
		}
	}
	if (!huser[0] || (!strcmp(huser, r->hash) && strcmp(hsys, r->hash))) {
		if (copy_file(src, dst) < 0)
			return -1;
		snprintf(r->hash, sizeof r->hash, "%s", hsys);
	} else if (strcmp(huser, r->hash) && strcmp(hsys, r->hash)) {
		fprintf(stderr, "themer: %s: yours (edited); the built-in changed since, see %s\n", dst, src);
		snprintf(r->hash, sizeof r->hash, "%s", hsys);  /* said once */
	} else if (!strcmp(huser, r->hash)) {
		snprintf(r->hash, sizeof r->hash, "%s", hsys);
	}
	pair_stamp(src, dst, r->stamp, sizeof r->stamp);
	return 0;
}

int
template_sync(void)
{
	static Seeded rec[MAX_SEEDED];
	char user[PATH_MAX], sets[MAX_NAMES][NAME_LEN], dirs[MAX_NAMES][NAME_LEN];
	char files[MAX_NAMES][NAME_LEN], p[PATH_MAX];
	Roots r;
	const char *sys;
	int nrec, ns, bad = 0;

	roots(&r);
	if (r.user < 0)
		return 0;
	/* the built-ins come from $THEMER_TEMPLATES when set (a source tree), else the system */
	sys = r.dir[r.user + 1];
	snprintf(user, sizeof user, "%s", r.dir[r.user]);
	nrec = seeded_load(user, rec, MAX_SEEDED);
	/* only what the system directory has: the built-in sets */
	{
		DIR *d = opendir(sys);
		struct dirent *e;

		ns = 0;
		if (!d)
			return 0;
		while ((e = readdir(d)) && ns < MAX_NAMES)
			if (valid_set(e->d_name) && strlen(e->d_name) < NAME_LEN)
				snprintf(sets[ns++], NAME_LEN, "%.*s", NAME_LEN - 1, e->d_name);
		closedir(d);
	}
	for (int i = 0; i < ns; i++) {
		DIR *d;
		struct dirent *e;
		int nd = 0;

		if (join(p, sizeof p, sys, sets[i], NULL, NULL) < 0 || !(d = opendir(p)))
			continue;
		while ((e = readdir(d)) && nd < MAX_NAMES)
			if (valid_name(e->d_name) && strlen(e->d_name) < NAME_LEN)
				snprintf(dirs[nd++], NAME_LEN, "%.*s", NAME_LEN - 1, e->d_name);
		closedir(d);
		/* the set's manifest, beside its directories */
		if (join(p, sizeof p, sys, sets[i], "set.conf", NULL) == 0 && !access(p, R_OK) &&
		    sync_file(user, sys, sets[i], "", "set.conf", rec, &nrec) < 0)
			bad++;
		for (int k = 0; k < nd; k++) {
			DIR *dd;
			int nf = 0;

			if (join(p, sizeof p, sys, sets[i], dirs[k], NULL) < 0 || !(dd = opendir(p)))
				continue;
			while ((e = readdir(dd)) && nf < MAX_NAMES)
				if (valid_file(e->d_name) && strlen(e->d_name) < NAME_LEN)
					snprintf(files[nf++], NAME_LEN, "%.*s", NAME_LEN - 1, e->d_name);
			closedir(dd);
			for (int f = 0; f < nf; f++)
				if (sync_file(user, sys, sets[i], dirs[k], files[f], rec, &nrec) < 0)
					bad++;
		}
	}
	/* a mirrored file whose built-in is gone (a set or file dropped upstream) goes
	 * too, if it is still as themer wrote it; an edited one is the user's and stays */
	for (int i = 0; i < nrec; i++) {
		char h[17];

		if (rec[i].seen)
			continue;
		if (snprintf(p, sizeof p, "%s/%s", user, rec[i].rel) >= (int)sizeof p)
			continue;
		file_hash(p, h, sizeof h);
		if (h[0] && strcmp(h, rec[i].hash))
			continue;
		if (!h[0] || unlink(p) == 0) {
			char *slash;

			/* and the directories it leaves empty */
			while ((slash = strrchr(p, '/')) && slash > p + strlen(user)) {
				*slash = '\0';
				if (rmdir(p) < 0)
					break;
			}
		}
		memmove(&rec[i], &rec[i + 1], (nrec - i - 1) * sizeof rec[0]);
		nrec--;
		seeded_dirty = 1;
		i--;
	}
	if (seeded_dirty && seeded_save(user, rec, nrec) < 0)
		bad++;
	return bad ? -1 : 0;
}
