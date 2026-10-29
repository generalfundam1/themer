/* themer.h - internal interfaces of the host. Modules see only themer_module.h. */
#ifndef THEMER_H
#define THEMER_H

#include <stddef.h>
#include <stdio.h>
#include <limits.h>
#include "themer_module.h"

#ifdef __cplusplus
extern "C" {
#endif

/* install locations; the Makefile sets them from PREFIX */
#ifndef SYS_THEMES_DIR
#define SYS_THEMES_DIR    "/usr/local/share/themer/themes"
#endif
#ifndef SYS_TEMPLATES_DIR
#define SYS_TEMPLATES_DIR "/usr/local/share/themer/templates"
#endif
#ifndef SYS_MODULES_DIR
#define SYS_MODULES_DIR   "/usr/local/lib/themer/modules"
#endif

#define NAME_LEN     64
#define MAX_ENV      64
#define MAX_MODULES  32
#define MAX_MODOPTS  256
#define MAX_OPTS     32          /* options of one module, the host's mode included */
#define MAX_NAMES    64          /* template sets or files listed at once */
#define MAX_THEMES   1024        /* colour schemes listed at once */

/* the roles a theme file defines, in the order of color_names */
enum { C_BG, C_FG, C_SURF, C_ACC, C_ACCFG, C_URG, C_WARN, C_N };
enum { MODE_DARK, MODE_LIGHT };

typedef struct { char name[64]; char value[256]; } EnvVar;

/* a theme file holds a [dark] and a [light] section */
typedef struct {
	char name[NAME_LEN];
	char hex[2][C_N][TM_COLOR_LEN];
	char ansi[2][16][TM_COLOR_LEN];
} Theme;

typedef struct { char name[NAME_LEN]; int on; } ModState;

/* a module option the user changed: key is "module.option" */
typedef struct { char key[2 * NAME_LEN]; char value[NAME_LEN]; } OptState;

/* everything the user has chosen; saved in ~/.config/themer/state */
typedef struct {
	int mode;
	Theme th;
	char ui_font[64];
	int ui_size;
	char mono_font[64];
	int mono_size;
	char tty_font[64];
	char icons[64];
	char cursor[64];
	int cursor_size;
	int web_port;                   /* where themer-web listens on localhost */
	char hinting[16];               /* none, slight, medium, full */
	int antialias;
	char rgba[8];                   /* none, rgb, bgr, vrgb, vbgr */
	char set[NAME_LEN];             /* the template set every module renders from */
	char color[C_N][TM_COLOR_LEN];  /* "" = use theme value */
	EnvVar env[MAX_ENV];
	int nenv;
	ModState mods[MAX_MODULES];     /* explicit on/off; others use default_on */
	int nmods;
	OptState optv[MAX_MODOPTS];     /* changed module options; others use the default */
	int nopt;
} Config;

/* the effective colors of one mode: the theme, overrides, and the derived text colors */
typedef struct {
	char hex[C_N][TM_COLOR_LEN];
	char ansi[16][TM_COLOR_LEN];
	char line[TM_COLOR_LEN];        /* outline color, derived from fg and bg */
	char on_urg[TM_COLOR_LEN];      /* text on the urgent color */
	char on_warn[TM_COLOR_LEN];     /* text on the warning color */
} Scheme;

typedef int  (*GenFn)(const Config *, const Scheme *);
typedef void (*EmitFn)(FILE *, const Config *, const Scheme *);
typedef struct { const char *name; GenFn fn; } Target;

/* theme.c: theme files and the readability rules */
extern const char *color_names[C_N];
int  theme_read(const char *name, Theme *t);
int  theme_load(const char *name, Theme *t);
int  theme_enforce(Theme *t, int *adjusted);
int  theme_list(char names[][NAME_LEN], int max);
void theme_scheme(const Config *c, Scheme *s);

/* scheme.c: colour schemes in (from .theme, Gogh, Xresources, kitty, foot or
 * alacritty colors; the missing side derived) and out (the user's deleted, a
 * built-in one hidden). import returns how many it wrote, -1 on failure. */
int  scheme_import(const char *path, const char *name, FILE *report);
int  scheme_remove(const char *name);
int  scheme_restore(const char *name);        /* NULL: every hidden one */

/* state.c: defaults and the state file */
int  config_defaults(Config *c);
int  state_load(Config *c);
int  state_save(const Config *c);
int  config_same(const Config *a, const Config *b);   /* the same settings */
/* whether the state file changed since this process last read or wrote it (another
 * front end saved), and merging: into disk, the settings mine changed from base */
int  state_changed(void);
void config_merge(Config *disk, const Config *mine, const Config *base);
void env_set(Config *c, const char *name, const char *value);
int  config_module_on(const Config *c, const char *name, int dflt);
void config_set_module(Config *c, const char *name, int on);
const char *config_option(const Config *c, const char *key);
void config_set_option(Config *c, const char *key, const char *value);
void config_unset_option(Config *c, const char *key);

/* registry.c: the loaded modules, their options, and apply */
void modules_load(void);
int  themer_apply(const Config *c);
int  themer_module_count(void);
int  themer_module_find(const char *name);
const char *themer_module_name(int i);
const char *themer_module_desc(int i);
const char *themer_module_templates(int i);
int  themer_module_on(const Config *c, int i);
int  themer_module_present(const Config *c, int i);   /* 1 unless its present() says no */
int  themer_module_set(Config *c, int i, int on);
int  themer_module_vars(int i, const TmTheme *t, TmVar *v, int max);
void themer_view(const Config *c, const Scheme *s, TmTheme *t);
/* the mode a module is rendered in (its "mode" option or the global one), and the
 * view it is handed in that mode */
int  themer_module_mode(const Config *c, int i);
void themer_module_view(const Config *c, int i, Scheme *s, TmTheme *t);
int  themer_option_count(int i);
int  themer_option_find(int i, const char *name);
const TmOption *themer_option(int i, int j);
const char *themer_option_value(const Config *c, int i, int j);
int  themer_option_set(Config *c, int i, int j, const char *value);   /* NULL: the default */
int  themer_option_is_set(const Config *c, int i, int j);  /* the user set it */

/* manifest.c: runs a module's load (after undoing its previous changes) or its undo */
int  mod_load(const TmModule *m, const TmTheme *t);
int  mod_unload(const TmModule *m);

/* template.c: template sets on disk */
int  template_sets(const char *dir, char names[][NAME_LEN], int max);
int  template_files(const char *dir, const char *set, char names[][NAME_LEN], int max);
int  template_dirs(char names[][NAME_LEN], int max);
int  template_all_sets(char names[][NAME_LEN], int max);
int  template_has(const char *dir, const char *set);
int  template_builtin(const char *set);
int  template_copy(const char *dir, const char *from, const char *to);
/* mirrors the built-in sets into ~/.config/themer/templates (see template.c) */
int  template_sync(void);

/* palette.c: the shared template variables, derived from a theme */
#define MAX_SHARED 96
typedef struct {
	char color[24][TM_COLOR_LEN];   /* storage the derived colors point into */
	char num[5][16];
	char ansi_name[16][8];
} SharedStore;
int  shared_vars(const TmTheme *t, SharedStore *st, TmVar *v, int max);

/* targets.c: built-in targets that are not modules */
int  gen_env(const Config *, const Scheme *);
int  gen_tty(const Config *, const Scheme *);
int  gen_grub(const Config *, const Scheme *);

/* util.c */
/* Replacing a file whole, atomically: afile_open makes a temporary file beside the
 * real one (a symlink is followed, so a linked dotfile stays linked; the original's
 * permissions are kept), afile_commit renames it into place, afile_abort drops it.
 * A failure halfway never leaves the real file truncated. Setting sync before the
 * commit also flushes it to disk first: for what cannot be made again (the state,
 * the user's own files and their kept originals, the undo logs), not for files a
 * module generates, which the next apply rewrites anyway. */
typedef struct { char path[PATH_MAX], tmp[PATH_MAX + 16]; FILE *f; int sync; } AFile;
FILE *afile_open(AFile *a, const char *path, int binary);
int   afile_commit(AFile *a);
void  afile_abort(AFile *a);
int  path_under_home(char *out, size_t n, const char *rel);
int  mkdirs(const char *path);
int  write_gen(const char *path, EmitFn fn, const Config *c, const Scheme *s);
int  write_bytes(const char *path, const void *buf, size_t len);
int  kv_set_file(const char *path, const char *key, const char *val);
int  run(const char *cmd);
int  is_root(void);
int  valid_name(const char *s);
int  valid_text(const char *s, size_t max);   /* printable, non-empty, shorter than max */

/* icons.c: installed icon themes (directories with an index.theme), sorted; with
 * cursors = 1 only those that hold cursors. Looks in ~/.icons, ~/.local/share/icons,
 * /usr/share/icons, /usr/local/share/icons. */
int  icon_themes_scan(char names[][NAME_LEN], int max, int cursors);
/* the directory of an icon theme, or -1 if it is not installed */
int  icon_theme_dir(const char *name, char *out, size_t n);
/* the valid values of the font rendering settings */
extern const char *const hinting_values[4];
extern const char *const rgba_values[5];

/* fonts.c */
int  fonts_scan(char names[][NAME_LEN], int max);
int  fonts_mono(char names[][NAME_LEN], int max);

/* preview.c */
int  font_preview_write(const Config *c, const Scheme *s, const char *family, int size,
                        char *out, size_t n);

/* setimport.c: an existing theme into a set; a set rendered for every theme; set.conf */
int  set_import(const Config *c, const char *set, const char *module, const char *path, FILE *report);
int  set_check(const Config *c, const char *set, FILE *report);
int  set_info(const char *set, const char *key, char *out, size_t n);

/* front.c: the settings as every front end changes them (CLI, TUI, themer-gtk,
 * themer-qt). Setters return 0, or -1 when the value is refused and nothing changes;
 * front_apply applies and saves, returning the number of failures. */
int  front_set_theme(Config *c, const char *name);
int  front_set_set(Config *c, const char *name);           /* the template set */
const char *front_set_title(const char *set, char *out, size_t n);  /* its display name */
int  front_set_font(Config *c, int mono, const char *family, int size); /* size 0 keeps it */
int  front_set_icons(Config *c, const char *name);
int  front_set_cursor(Config *c, const char *name, int size); /* NULL or 0 keeps one */
int  front_set_hinting(Config *c, const char *value);
int  front_set_rgba(Config *c, const char *value);
void front_set_antialias(Config *c, int on);
int  front_apply(Config *c);
/* Keeping a long-running front end in step with the others. front_track names its
 * settings (cfg, being browsed) and what was last saved (base). front_sync then
 * reads the state file when another front end changed it and merges: their
 * changes come in, cfg's unapplied ones stay; it returns 1 when anything changed
 * (redraw). front_apply and every immediate save (a module, an option, the port)
 * sync first, so no front end writes back settings it read before another
 * changed them; an immediate save writes base with the one change, never cfg's
 * unapplied browsing. */
void front_track(Config *cfg, Config *base);
/* a module loaded or unloaded now and saved (from base when tracked) */
int  front_module_set(Config *c, int module, int on);
int  front_sync(void);
int  front_save_now(Config *c);
int  front_set_copy(const char *from, const char *to, FILE *report);
int  front_option_set(Config *c, int module, int option, const char *value, char *msg, size_t n);
/* back to the option's default (for template: following the global set again) */
int  front_option_reset(Config *c, int module, int option, char *msg, size_t n);
/* How every front end lays out a module's options: the server section (server,
 * port), the look (mode, font, template and every other setting), then the parts it
 * themes (the on|off options: gtk2, qt6, a stylus site). */
enum { OG_LOOK, OG_PARTS, OG_N };
extern const char *const front_option_groups[OG_N];
int  front_option_group(int module, int option);
int  front_option_is_switch(int module, int option);   /* on|off */
/* The control every front end draws for an option, so they look alike: a switch
 * (on|off), buttons side by side (a few short choices, the chosen one in the
 * accent), a list (the template set; long choice lists), a slider (a number in a
 * range), or a text field. */
enum { OC_SWITCH, OC_BUTTONS, OC_LIST, OC_SLIDER, OC_TEXT };
int  front_option_control(int module, int option);
/* themer-web: its port (in the state), whether it runs, and starting (detached) or
 * stopping it; 0 on success. front_set_web_port moves a running server along. */
int  front_web_port(const Config *c);
int  front_set_web_port(Config *c, int port);
int  front_web_running(int port);
int  front_web_start(int port);
int  front_web_stop(int port);
/* the modules in name order: fills order with indexes, returns the count */
int  front_module_order(int *order, int max);
/* renders templates/<set>/<dir>/<file> for the configuration as it is now, with the
 * named module's own variables. Returns 0, or -1 with a message on stderr. */
int  front_render(const Config *c, const char *module, const char *dir, const char *file,
                  char *out, size_t n);

int  front_scheme_remove(Config *c, const char *name, char *msg, size_t n);

/* opens a file or URL in the default browser (xdg-open), detached; 0 on success */
int  front_open(const char *target);
/* the stylus module's sites that are on (none when the module is off); returns the count */
int  front_stylus_sites(const Config *c, const char **sites, int max);
/* puts a site's userstyle (every site's, when site is NULL) in front of Stylus: starts
 * themer-web when it is down, then opens the style's address in the default browser,
 * where Stylus offers to install it. Returns 0 or -1, with a line for the user in msg. */
int  front_stylus_install(const Config *c, const char *site, char *msg, size_t n);

/* A single color over the scheme's (role is a C_* index; hex is rrggbb, with or
 * without #; NULL or "" goes back to the scheme's), and a role by its name. */
int  front_set_color(Config *c, int role, const char *hex);
int  front_color_role(const char *name);
/* the console (TTY) font, and the installed ones (kbd's consolefonts), sorted */
int  front_set_tty_font(Config *c, const char *name);
int  front_tty_fonts(char names[][NAME_LEN], int max);
/* an environment variable for the session (environment.d): an empty value removes
 * it. Refuses a bad name or a value that is not one line, with a reason in msg. */
int  front_env_set(Config *c, const char *name, const char *value, char *msg, size_t n);
/* An action shown beside an option, in every front end: its label ("Install" for a
 * stylus site that is on) or NULL; the page it opens, as a themer-web path (the web
 * front end links to it); and running it (the others: front_stylus_install). */
const char *front_option_action(const Config *c, int module, int option);
int  front_option_link(const Config *c, int module, int option, char *out, size_t n);
int  front_option_run(const Config *c, int module, int option, char *msg, size_t n);

/* tui.c */
void tui_run(Config *c);

/* startup shared by every front end: the defaults, the state file (or the
 * compile-time environment when there is none), and the modules. Returns 0, or -1
 * if the default theme is missing. */
int  themer_startup(Config *c);

#ifdef __cplusplus
}
#endif

#endif
