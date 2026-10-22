/* Compile-time settings. make copies this to config.h; edit that. */

#define UI_FONT    "Adwaita Sans"
#define UI_SIZE    11
#define MONO_FONT  "monospace"
#define MONO_SIZE  10
#define TTY_FONT   "ter-v16n"
#define ICON_THEME "Adwaita"
#define CURSOR_THEME "Adwaita"
#define CURSOR_SIZE 24
#define HINTING    "slight"     /* none, slight, medium, full */
#define ANTIALIAS  1
#define RGBA       "rgb"        /* none, rgb, bgr, vrgb, vbgr */
#define DEF_THEME  "habamax"

/* environment variables used when there is no state file yet */
#define DEF_ENV { {"QT_QPA_PLATFORMTHEME", "qt5ct"}, {"MOZ_ENABLE_WAYLAND", "1"} }
