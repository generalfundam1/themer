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
#define MAX_MODOPTS  128
#define MAX_NAMES    64          /* themes, template sets, or files listed at once */

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

/* state.c: defaults and the state file */
int  config_defaults(Config *c);
int  state_load(Config *c);
int  state_save(const Config *c);
void env_set(Config *c, const char *name, const char *value);
int  config_module_on(const Config *c, const char *name, int dflt);
void config_set_module(Config *c, const char *name, int on);
const char *config_option(const Config *c, const char *key);
void config_set_option(Config *c, const char *key, const char *value);

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
int  themer_option_count(int i);
int  themer_option_find(int i, const char *name);
const TmOption *themer_option(int i, int j);
const char *themer_option_value(const Config *c, int i, int j);
int  themer_option_set(Config *c, int i, int j, const char *value);

/* manifest.c: runs a module's load (after undoing its previous changes) or its undo */
int  mod_load(const TmModule *m, const TmTheme *t);
int  mod_unload(const TmModule *m);

/* template.c: template sets on disk */
int  template_sets(const char *dir, char names[][NAME_LEN], int max);
int  template_files(const char *dir, const char *set, char names[][NAME_LEN], int max);
int  template_dirs(char names[][NAME_LEN], int max);
int  template_all_sets(char names[][NAME_LEN], int max);
int  template_has(const char *dir, const char *set);
int  template_copy(const char *dir, const char *from, const char *to);
/* mirrors the built-in sets into ~/.config/themer/templates (see template.c) */
int  template_sync(void);

/* palette.c: the shared template variables, derived from a theme */
#define MAX_SHARED 96
typedef struct {
	char color[20][TM_COLOR_LEN];   /* storage the derived colors point into */
	char num[5][16];
	char ansi_name[16][8];
} SharedStore;
int  shared_vars(const TmTheme *t, SharedStore *st, TmVar *v, int max);

/* targets.c: built-in targets that are not modules */
int  gen_env(const Config *, const Scheme *);
int  gen_tty(const Config *, const Scheme *);
int  gen_grub(const Config *, const Scheme *);

/* util.c */
int  path_under_home(char *out, size_t n, const char *rel);
int  mkdirs(const char *path);
int  write_gen(const char *path, EmitFn fn, const Config *c, const Scheme *s);
int  write_bytes(const char *path, const void *buf, size_t len);
int  kv_set_file(const char *path, const char *key, const char *val);
int  run(const char *cmd);
int  is_root(void);
int  valid_name(const char *s);

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
int  front_set_font(Config *c, int mono, const char *family, int size); /* size 0 keeps it */
int  front_set_icons(Config *c, const char *name);
int  front_set_cursor(Config *c, const char *name, int size); /* NULL or 0 keeps one */
int  front_set_hinting(Config *c, const char *value);
int  front_set_rgba(Config *c, const char *value);
void front_set_antialias(Config *c, int on);
int  front_apply(Config *c);
int  front_set_copy(const char *from, const char *to, FILE *report);
int  front_option_set(Config *c, int module, int option, const char *value, char *msg, size_t n);
/* the modules in name order: fills order with indexes, returns the count */
int  front_module_order(int *order, int max);
/* renders templates/<set>/<dir>/<file> for the configuration as it is now, with the
 * named module's own variables. Returns 0, or -1 with a message on stderr. */
int  front_render(const Config *c, const char *module, const char *dir, const char *file,
                  char *out, size_t n);

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
