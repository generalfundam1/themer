# themer

**One theme, one dark/light switch, applied to the whole desktop — and undone the same way.**

themer is a suckless-style theme manager written in C99. It takes a colour theme and a
mode (dark or light) and writes the configuration of everything on the desktop from
them: GTK 2/3/4, Qt 5/6, Firefox, Thunderbird, sway or i3, waybar, foot, nvim, fcitx5,
launchers, newt dialogs, the TTY. Every write is recorded, so unloading a module puts
the user's files back byte for byte.

It replaces nwg-look, qt5ct and qt6ct: icons, cursor, fonts and font rendering are
applied from the same state, and Qt programs are themed through themer's own platform
theme plugin.

```
themer                 # the TUI
themer-gtk             # the same, as a GTK window with a live preview
themer-qt              # ... or a Qt window
themer toggle          # flip dark/light, apply everywhere
themer theme gruvbox   # switch theme, apply everywhere
```

<p align="center">
  <img src="screenshots/themer-gtk-dark.png" width="49%" alt="themer-gtk, gruvbox dark">
  <img src="screenshots/themer-gtk-light.png" width="49%" alt="themer-gtk, gruvbox light">
</p>
<p align="center"><sub>themer-gtk: the same theme in dark and light. The preview on the right is the GTK theme the gtk module writes, drawn by the window itself.</sub></p>

<p align="center">
  <img src="screenshots/firefox-gruvbox-dark.png" width="100%" alt="Firefox chrome, gruvbox dark">
  <img src="screenshots/firefox-nord-light.png" width="100%" alt="Firefox chrome, nord light">
</p>
<p align="center"><sub>Firefox 140 with the firefox module: three flat rows, tabs like sway's, the URL bar in the terminal font.</sub></p>

## Contents

- [Build and install](#build-and-install)
- [Usage](#usage)
- [Front ends](#front-ends)
- [What apply covers](#what-apply-covers)
- [Qt](#qt)
- [Environment](#environment)
- [Modules](#modules)
- [Templates](#templates)
- [Writing a template set](#writing-a-template-set)
- [Design language](#design-language)
- [Readability](#readability)
- [Firefox](#firefox)
- [Thunderbird](#thunderbird)
- [Themes](#themes)
- [Fonts](#fonts)
- [Writing a module](#writing-a-module)
- [Upgrading](#upgrading)
- [Status and license](#status-and-license)

## Build and install

```sh
make                  # host, modules, and whichever front ends the system can build
make check            # render every template, then an apply/unload round trip in a throwaway HOME
sudo make install     # to PREFIX (/usr/local); an earlier install is removed first
sudo make uninstall   # removes all of that; ~/.config/themer is yours and stays
```

`make` builds `themer-gtk` when `pkg-config` finds `gtk+-3.0`, `themer-qt` when it
finds `Qt6Widgets` or `Qt5Widgets` and a C++ compiler, and the Qt platform theme
plugin for each Qt major it finds. It prints what it found; `GTK=0` or `QT=0` skips
one on purpose. The only hard dependencies are a C compiler and libc.

| Make target | What it does |
|---|---|
| `make` | host binary, modules, front ends, Qt plugin |
| `make check` | templates rendered for every theme and mode; apply/unload round trip (`test/run.sh`) |
| `FIREFOX=/path/to/firefox sh test/firefox.sh [theme] [dark\|light]` | renders the chrome in a real Firefox under Xvfb; screenshots and toolbar geometry to `test/firefox/out/` |
| `sudo make install` | binaries to `PREFIX/bin`, themes/templates/icons to `PREFIX/share/themer`, modules to `PREFIX/lib/themer/modules`, the Qt plugin to `PREFIX/lib/themer/qt`, the module header to `PREFIX/include/themer` |
| `make dist` | `themer-VERSION.tar.gz` from the git tree |

**Arch:** `dist/PKGBUILD` and `dist/themer.install`. Run `make dist`, then `makepkg -si`
next to the tarball. The Qt plugin is built against the installed Qt and must be
rebuilt after a Qt minor upgrade; a package makes that one command.

`themer -v` prints the version; `VERSION` is set at the top of the Makefile.

### Layout

| Directory | Contents |
|---|---|
| `src/` | the host: one binary. `main.c` the command line; `state.c` the state file; `registry.c` finds modules and runs apply; `manifest.c` the `tm_*` helpers modules write with, and the undo log; `template.c` and `palette.c` the templates and shared variables; `theme.c` the theme files and readability rules; `targets.c` the env, tty, and grub targets; `front.c` the layer every front end calls; `tui.c`, `fonts.c`, `preview.c` the interactive parts |
| `gui/` | `themer-gtk` (GTK 3, C) and `themer-qt` (Qt 5 or 6, C++); `common.c` is what they share; `qpa/` the Qt platform theme plugin |
| `include/` | `themer_module.h`, the API modules are built against |
| `modules/` | one `.c` per module; `wm.h`, `launcher.h`, `icons.h` are shared code |
| `templates/` | `templates/<set>/<dir>/<file>`; a set is one directory (`themer_chicago` is built in), `<dir>` is named by the module |
| `themes/` | `<name>.theme`, a `[dark]` and a `[light]` section each |
| `icons/` | Adwaita's symbolic icons, the fallback for the Firefox toolbar |
| `test/` | `make check` |

## Usage

```
themer                              TUI
themer-gtk | themer-qt [PAGE]       the same settings in a window, with a live preview;
                                    PAGE: theme, fonts, icons, cursor, or modules
themer apply                        re-apply everything and save state (run at login)
themer toggle                       flip dark/light and apply
themer dark | light                 set mode and apply
themer theme NAME                   switch theme and apply
themer list                         list themes
themer check                        list every color the readability rules adjust
themer env NAME=VALUE               set an environment variable and apply
themer icons [NAME]                 show (current first) or set the icon theme and apply
themer cursor [NAME [SIZE]]         show or set the cursor theme and size and apply
themer font ui|mono FAMILY [SIZE]   set the interface or terminal font and apply
themer render [hinting none|slight|medium|full] [antialias on|off]
              [rgba none|rgb|bgr|vrgb|vbgr]
                                    show or set font rendering and apply
themer set                          show the template set
themer set NAME                     switch every module to template set NAME and apply
themer set list                     list the sets and the modules each covers
themer set copy FROM TO             copy a set under a new name, to edit
themer set import NAME MODULE PATH  turn an existing theme into (part of) a set
themer set check NAME               render every file of NAME for every theme
themer module list                  list modules, on/off, description
themer module load|unload NAME      load (write its files) or unload (remove them) now
themer module options NAME          list a module's options
themer module option NAME KEY VALUE set one; a loaded module reloads at once
themer template list DIR [SET]      list template sets, or the files in SET
themer template render MODULE SET FILE [OUT]
                                    render one template with the current theme
themer -v                           version
```

**Lookup, most specific first:**

| | |
|---|---|
| themes | `$THEMER_THEMES`, `~/.config/themer/themes`, `PREFIX/share/themer/themes` |
| modules | `$THEMER_MODULES`, `~/.config/themer/modules`, `PREFIX/lib/themer/modules` |
| templates | `~/.config/themer/templates`, `$THEMER_TEMPLATES`, `PREFIX/share/themer/templates` (built-in sets are mirrored into the first; see [Templates](#templates)) |
| icons | `$THEMER_ICONS`, `PREFIX/share/themer/icons` (the Firefox fallback only) |

Running from the source tree:

```sh
THEMER_THEMES=$PWD/themes THEMER_MODULES=$PWD/modules \
THEMER_TEMPLATES=$PWD/templates THEMER_ICONS=$PWD/icons ./themer
```

**State** lives in `~/.config/themer/state`: theme, mode, template set, fonts, icon
theme, cursor theme and size, font rendering, environment, which modules are on, and
option values. Compile-time defaults are in `config.def.h`.

## Front ends

The CLI, the TUI, `themer-gtk` and `themer-qt` are four faces of one layer
(`src/front.c`); every setting and action is reachable from each, and Apply runs the
same `themer apply` whichever one you used.

`themer-gtk` and `themer-qt` are the TUI as a window, like nwg-look: pages for the
theme, the fonts, the icon theme, the cursor and the modules on the left, a preview on
the right, Apply at the bottom (Ctrl+S; Revert to applied undoes browsing), File and
Help menus. The preview is the real thing, not a mock-up: the window restyles itself
from the `gtk.css` (or `qt.qss` and palette) the gtk (or qt) module would write for the
selected theme, so what the widgets look like there is what every GTK (or Qt) app will
look like after Apply. The Icons page shows the selected icon theme's own icons, the
Cursor page draws the cursor theme's own files at the chosen size, the Terminal tab the
terminal font and colours, and the swatches the roles and the sixteen terminal
colours. Nothing is written until Apply. Closing with unapplied changes asks first.

Template sets from the window: on the Theme page, **New set** copies the selected set
under a new name into `~/.config/themer/templates/`, **Import theme** turns a theme you
already have into part of a set (a GTK theme directory, a `.qss`, a `chrome/`
directory...), **Check** renders the selected set for every theme and shows the report.
The TUI has the same under *Template set* (`n`, `i`, `c`). Module options are on the
Modules page and take effect at once; `template` pins one module to a set of its own.

<p align="center">
  <img src="screenshots/themer-qt-dark.png" width="49%" alt="themer-qt, nord dark">
  <img src="screenshots/themer-qt-cursor.png" width="49%" alt="themer-qt, cursor page">
</p>
<p align="center">
  <img src="screenshots/themer-gtk-icons.png" width="49%" alt="themer-gtk, icons page">
  <img src="screenshots/themer-gtk-modules.png" width="49%" alt="themer-gtk, modules page">
</p>
<p align="center"><sub>themer-qt (nord dark) and themer-gtk (gruvbox light): the Cursor page draws the cursor theme's own files, the Icons page the icon theme's, and the Modules page holds every module's options.</sub></p>

`themer-qt` needs no moc. `make gui` repeats what the build found; `make themer-gtk` or
`make themer-qt` builds one on purpose.

## What apply covers

The ground nwg-look and qt5ct/qt6ct cover, from one state, whichever front end set it:

| Module | Writes |
|---|---|
| **gtk** | `~/.config/gtk-3.0/settings.ini` and `gtk-4.0/settings.ini`: theme, icon theme, cursor theme and size, font, prefer-dark, Xft antialias/hinting/hintstyle/rgba; the same keys in `~/.gtkrc-2.0` (GTK 2) and `~/.config/xsettingsd/xsettingsd.conf` (xsettingsd gets SIGHUP); the `org.gnome.desktop.interface` gsettings keys (gtk-theme, icon-theme, cursor-theme, cursor-size, font-name, monospace-font-name, color-scheme, font-antialiasing, font-hinting, font-rgba-order) when gsettings is installed (`gsettings=off` turns that off) |
| **qt** | `~/.config/themer/qt/theme.conf` and `themer.qss` for themer's platform theme plugin (style, palette, icon theme, fonts, stylesheet), and `qt5ct.conf` / `qt6ct.conf` with the same where those tools are installed. qt5ct's Interface page is yours and is left as it is |
| **cursor** | `~/.icons/default/index.theme` and `~/.local/share/icons/default/index.theme` (`Inherits=`), `XCURSOR_THEME` and `XCURSOR_SIZE` in environment.d, `Xcursor.theme/size` in `~/.Xresources` (merged with xrdb when an X server runs); sway gets `seat * xcursor_theme` from the sway module, i3 reads the Xresources |
| **fontconfig** | `~/.config/fontconfig/conf.d/50-themer-rendering.conf`: antialias, hinting, hintstyle, rgba, lcdfilter, for everything that is not GTK |

Icon and cursor themes are listed from `~/.icons`, `~/.local/share/icons`,
`/usr/share/icons` and `/usr/local/share/icons`; a cursor theme is one with a `cursors/`
directory. Any name can also be typed.

## Qt

Qt programs take a palette, fonts, an icon theme and a style only from a *platform
theme* plugin, which is why qt5ct and qt6ct exist. themer brings its own:
`gui/qpa/themerqpa.cpp`, built once per Qt major that `make` finds and installed under
`PREFIX/lib/themer/qt/qt5/` and `qt6/`. The qt module writes
`~/.config/themer/qt/theme.conf` (style, icon theme, fonts, the palette) and
`themer.qss`, and sets `QT_QPA_PLATFORMTHEME=themer` and `QT_PLUGIN_PATH` in its
environment file. The plugin watches those files, so an apply restyles running Qt
programs at once.

Where the plugin is not installed for your Qt, the module falls back to qt5ct (and
qt6ct) when they are installed; the `platformtheme` option forces either. Qt reads the
environment at start — see [Environment](#environment).

## Environment

Several things are environment variables: `QT_QPA_PLATFORMTHEME` and `QT_PLUGIN_PATH`,
`XCURSOR_THEME` and `XCURSOR_SIZE`, the fcitx input-method variables, and whatever you
set with `themer env`. themer writes them to `~/.config/environment.d/*themer*.conf`,
which reaches programs when the session is started through systemd (a display manager,
or sway as a user service).

When you start the compositor yourself from a tty, nothing reads environment.d. Source
the script themer also writes, from your shell profile, before the compositor starts:

```sh
. ~/.config/themer/env.sh      # in ~/.profile, ~/.zprofile, or ~/.bash_profile
```

Check with `echo $QT_QPA_PLATFORMTHEME` in a terminal inside the session: empty means
the variables are not reaching your programs, and Qt will look plain however the rest
is set up. themer never writes `/etc/environment`; if that is where your session reads
from, the two lines to add are:

```
QT_QPA_PLATFORMTHEME=themer
QT_PLUGIN_PATH=/usr/local/lib/themer/qt/qt5:/usr/local/lib/themer/qt/qt6
```

## Modules

**Detection.** A module says whether the program it themes is here (`present()` in the
module API: the binary on `PATH`, a config file, a profile, a socket). An absent one is
listed as *absent* by `themer module list`, shown as *(not installed)* in the TUI and
the GUIs, and skipped by apply; it stays on, so it takes effect as soon as the program
appears. Modules without a check (gtk, cursor, fontconfig, newt, st) always write; their
files are harmless where the program is missing.

All on by default unless noted:

| Module | Themes |
|---|---|
| **gtk** | a GTK 3/4 theme written to `~/.config/themer/gtk/`, reached by GTK through a link `~/.themes/<set>` named after the template set (`themer_chicago`, labelled "Themer Chicago"); no `GTK_THEME` is set, since that would override the file and go stale. Options `gtk2`, `gtk3`, `gtk4` turn each version off. Restarts the GTK file-chooser portal on apply so Firefox's file dialogs on Wayland follow without a logout. A GTK app keeps the theme it loaded at launch: restart it, or log out |
| **qt** | Qt 5 and 6: palette, flat stylesheet, icons, fonts, through the platform theme plugin; where qt5ct/qt6ct are installed their configs are written too. Options `qt5`, `qt6`, `platformtheme` |
| **cursor** | the mouse cursor theme and size: Xcursor default theme, `XCURSOR_*`, Xresources |
| **fontconfig** | font rendering for everything that reads fontconfig |
| **firefox** | the browser chrome (`chrome/userChrome.css`) and Firefox's own pages (`chrome/userContent.css`), as marked blocks in the profile |
| **thunderbird** | the mail window and the panes Thunderbird draws as content, the same way |
| **sway** | window and bar colours, the font, `$menu`, the seat cursor; loads only where sway is in use. Borders, gaps and titlebars stay in your own config |
| **i3** | the same for i3 |
| **waybar** | dwm-style bar; owns `style.css`, the original is kept as `style.css.themer-orig` and put back on unload. `config.jsonc` is not touched |
| **foot** | font and colours in `foot.ini` (`[colors-dark]` and `[colors-light]`, foot 1.15+, plus `initial-color-theme`) |
| **nvim** | a `themer` colorscheme, selected from the end of `init.lua` or `init.vim` |
| **fcitx** | fcitx5 input-method variables and candidate-window theme |
| **wmenu** | a launcher script, `~/.config/themer/bin/themer-wmenu`, in the theme's colours |
| **dmenu** | the same for dmenu |
| **newt** | `NEWT_COLORS` for nmtui and other newt dialogs, each colour mapped to the nearest terminal colour. Set in environment.d; copy the line to your shell profile for programs started elsewhere |
| **st** *(off)* | a colours header; st's `config.h` needs one edit, see `modules/st.c` |
| **example** *(off)* | the template module to copy |

Built-in targets, not modules: **env** (`environment.d/50-themer.conf`), **tty** (the
console palette and font, as root), **grub** (as root). They write system files the
undo log cannot cover.

**How modules change files.** Every write goes through the `tm_*` helpers in
`themer_module.h` and is recorded in `~/.local/state/themer/<module>.manifest`.
Unloading reverses the records: generated files are deleted (only while still generated
by themer), replaced files are put back from their `.themer-orig` copy, marked blocks
are cut out, ini keys and replaced lines get their old values, and directories themer
created are removed when empty. Applying runs unload-then-load, so re-applying never
duplicates anything. After a change each module's reload hook tells its running
program: sway and i3 are reloaded when their socket variable is set, waybar gets
SIGUSR2, xsettingsd gets SIGHUP.

### Module options

Set them in the TUI (module menu, then `o`), on the Modules page of the GUIs, or with
`themer module option`. Modules that render templates all have `template`, the set
name: unset, it follows the global template set; set, it pins that module to a set of
its own.

| Module | Option | Values | Default |
|---|---|---|---|
| gtk | `font` | `ui` \| `terminal` | `ui` |
| | `gtk2`, `gtk3`, `gtk4` | `on` \| `off` | `on` |
| | `gsettings`, `xsettingsd` | `on` \| `off` | `on` |
| qt | `font` | `ui` \| `terminal` | `ui` |
| | `qt5`, `qt6` | `on` \| `off` | `on` |
| | `platformtheme` | `auto` \| `themer` \| `qt5ct` | `auto` (the plugin when installed) |
| firefox | `font` | `ui` \| `terminal` | `ui` |
| | `icons` | `0`–`8`, glyph size in points above the text | `2` |
| | `style` | `image` (the theme's icons, recoloured) \| `mask` (every icon's shape in the bar ink) | `image` |
| | `recolor` | `all` \| `symbolic` | `all` |
| | `raster` | `color` \| `ink` | `color` |
| thunderbird | `font` | `ui` \| `terminal` | `ui` |
| waybar | `font` | `terminal` \| `ui` | `terminal` |
| | `padding` | `tight` \| `normal` \| `loose` | `normal` |
| fcitx, wmenu, dmenu | `font` | `ui` \| `terminal` | `ui` |
| sway, i3 | `font` | `ui` \| `terminal` | `ui` |
| | `menu` | `auto` \| `wmenu` \| `dmenu` \| `none` | `auto` |
| | `bar` | `auto` \| `on` \| `off` | `auto` (a bar only if the config has none) |
| example | `style` | `short` \| `long` | |

## Templates

A module's files come from templates. A set is one directory,
`~/.config/themer/templates/<set>/`, holding one subdirectory per module (modules with
the same output share one) and one file per output:

```
gtk/          gtk.css                        GTK 3 and 4
qt/           qt.qss, colors.conf, *.svg     the plugin, qt5ct and qt6ct
firefox/      userChrome.css, userContent.css
thunderbird/  tokens.css, chrome.css, content.css
wm/           themer.conf, bar.conf          sway and i3
launcher/     launcher                       wmenu and dmenu
waybar/       style.css
nvim/         themer.lua, select.lua, select.vim
fcitx/        theme.conf
newt/         newt.conf
st/           colors.h
example/      colors
set.conf      name=, description=, author=, url=   (optional)
```

The built-in set is **themer_chicago**. It is installed under
`PREFIX/share/themer/templates`, and every start of themer mirrors it into
`~/.config/themer/templates/themer_chicago/`, which is the copy that is used: edit it
there. The mirror keeps itself current without losing your edits:

- a file you have not touched follows the built-in when `make install` brings a new one;
- a file you changed is yours and stays (themer says so once if the built-in moved on,
  naming the new one under `PREFIX` to compare);
- a file you delete comes back;
- a copy left by an old build that differs is replaced and kept beside as `<file>.themer-old`;
- a mirrored file whose built-in is gone after an upgrade is removed again, unless you changed it.

How it tells the two apart is `.seeded` in that directory, a hash of each file as
themer last wrote it. Your own sets live next to it, with names of letters, digits,
`-` and `_`.

**Switching sets:** `themer set NAME` makes every module render from `NAME` and
applies. A set need not cover every module: what it lacks comes from `themer_chicago`,
so a set that restyles only Firefox still applies to the whole desktop. One module can
be pinned to another set with its `template` option.

To work on the look, edit `~/.config/themer/templates/themer_chicago/` and apply. For a
second variant:

```sh
themer set copy themer_chicago mine
# edit ~/.config/themer/templates/mine/
themer set mine
```

## Writing a template set

A set is a directory of ordinary theme files with `{{variables}}` in them. Nothing
else: no build, no manifest format, and a set may cover one module or all of them. The
quickest start is a theme you already use:

```sh
themer set import mylook gtk ~/.themes/Gruvbox-Material          # a GTK theme dir
themer set import mylook qt ~/.config/qt5ct/qss/mine.qss         # a Qt stylesheet
themer set import mylook firefox ~/.mozilla/firefox/x.default/chrome
themer set import mylook waybar ~/.config/waybar/style.css
themer set check mylook
themer set mylook
```

Import takes a file or a directory. It knows the usual layouts (a GTK theme's `gtk-3.0/`
and `gtk-4.0/`, a profile's `chrome/`) and otherwise takes the directory as it is. Each
text file goes under the name the module renders, with a `generated by themer` marker
line at the top, and **every colour in it that equals one of the current theme's colours
is replaced by that colour's variable**: `#282828` in a file imported under gruvbox dark
becomes `#{{bg}}`, `#83a598` becomes `#{{acc}}`. So import with the theme and mode the
look was made for, and the file follows every theme from then on. Colours that match
nothing stay literal and are listed in the report. Everything that is not text (images,
fonts, `assets/`) is copied as it is and placed beside the module's output on apply, so
`url("assets/x.png")` keeps working.

What each module directory takes, and what becomes of it on apply:

| Directory | File | On apply |
|---|---|---|
| `gtk/` | `gtk.css` | rendered to `~/.config/themer/gtk/gtk-3.0/gtk.css` and `gtk-4.0/gtk.css`. The set's `gtk.css` is the whole theme: there is no Adwaita underneath it |
| | `gtk4.css` | optional: GTK 4's `gtk.css` instead of the shared one |
| | `gtk-dark.css` | optional: what GTK 3 loads in dark mode |
| | any other `.css` / anything else | rendered / copied into both directories |
| `qt/` | `qt.qss` | to `~/.config/themer/qt/themer.qss` (and qt5ct/qt6ct's copies). `{{qt_assets}}` is the directory it lands in: `url({{qt_assets}}/x.svg)` for images, since a Qt stylesheet's `url()` is not relative to the stylesheet |
| | `*.svg` | rendered like templates (arrows, check marks in the theme's colours) and written beside the stylesheet |
| | `colors.conf` | the qt5ct/qt6ct palette; `{{qt_active}}`, `{{qt_disabled}}`, `{{qt_inactive}}` are the role lists built from the theme |
| `firefox/` | `userChrome.css`, `userContent.css` | a marked block at the end of the profile's files; other files copied into `chrome/`. Variables: `{{tab_font}}`, `{{tab_size}}`, `{{glyph_size}}`, and per toolbar icon `{{icon_X}}`, `{{icon_X_hover}}`, `{{raster_X}}`, `{{tint_X}}`, `{{tint_X_hover}}` (X: back forward reload stop home newtab menu downloads extensions star_off star reader identity close), data URIs of the icon theme |
| `thunderbird/` | `tokens.css`, `chrome.css`, `content.css` | tokens+chrome → userChrome, tokens+content → userContent. `{{font_family}}`, `{{font_size}}` |
| `wm/` | `themer.conf`, `bar.conf` | included from the sway/i3 config. `{{font_family}}`, `{{font_size}}`, `{{bar_block}}`, `{{cursor_block}}` |
| `launcher/` | `launcher` | the wmenu/dmenu wrapper script. `{{menu}}`, `{{args}}` |
| `waybar/` | `style.css` | replaces waybar's `style.css`. `{{font_family}}`, `{{font_size}}`, `{{ws_pad}}`, `{{mod_pad}}` |
| `nvim/` | `themer.lua`, `select.lua`, `select.vim` | the colorscheme and the line that selects it |
| `fcitx/` | `theme.conf` | the fcitx5 classic UI theme. `{{font_family}}`, `{{font_size}}` |
| `newt/` | `newt.conf` | `NEWT_COLORS`. `{{newt_colors}}` |
| `st/` | `colors.h` | st's `colorname[]` header |

Every file sees the shared variables plus its module's own. Rendered text files must
start with the marker (import adds it): that is how themer tells its own files from
yours when it overwrites and removes them. An unknown `{{name}}` is an error, reported
on apply, so a typo cannot reach the output.

| Shared variables | |
|---|---|
| roles | `bg fg surf acc accfg urg warn line on_urg on_warn` |
| tones | `face light shade pressed field hover_bg hover_fg disabled bar input bar_text input_text tab_text tab_active_text tab_hover_text hover_text glyph_hover` |
| terminal | `ansi0` … `ansi15` |
| other | `mode` (dark/light), `theme`, `icons`, `cursor`, `cursor_size`, `hinting`, `antialias` (1/0), `rgba`, `ui_font`, `ui_size`, `mono_font`, `mono_size`, `tty_font`, `pad_control`, `pad_field`, `pad_cell`, `pad_surface`, `pad_tip` |

To share a set, give people the directory; it goes under
`~/.config/themer/templates/<name>/` (or `PREFIX/share/themer/templates/` for a package)
and shows up in `themer set list`.

## Design language

The built-in set, themer_chicago, draws every module from the same roles (listed in
`themer_module.h`), so GTK, Qt, Firefox, sway/i3 and waybar look like one design:

| Role | Used for |
|---|---|
| `bg`, `fg` | windows, bars, menus; text on them |
| `surf` | controls, inactive tabs, fields in the bar |
| `acc`, `accfg` | the selected tab, focus rings, progress; text on acc |
| `urg`, `warn` | urgent windows and tabs; modes and warnings (text: `on_urg`, `on_warn`) |
| `line` | every 1px outline; outlines are square, never rounded |

The tones are derived from the roles in `src/palette.c`: `light` and `shade` are the
Chicago bevel (raised controls lit at top and left, shaded at bottom and right; sunken
fields the other way round); `hover_bg`/`hover_fg` are the accent when it reads 3:1 on
the background, otherwise the background moved toward the text until it does; `bar` and
`input` are the browser bar and its search field a step lighter; `pad_*` is one spacing
scale (4px 12px for buttons, tabs and menu items; 4px 6px for fields; 3px 6px for list
cells; 2px 0 for menus; 4px 8px for tooltips). Tabs look like sway/i3 tabs in GTK
notebooks, Firefox and sway's tabbed layout.

## Readability

Every theme is made readable when it is loaded, for both modes: text reads at 4.5:1 on
the surface it sits on, the selected-text colour reads on the accent, and the urgent,
warning and terminal colours read at 4.5:1 on the background. A colour that fails is
moved toward the theme's own text colour; black or white replaces it only where no move
works. `themer check` lists every colour that is adjusted. Text on the accent, the
urgent colour and the warning colour is chosen per colour (`accfg`, `on_urg`,
`on_warn`), since one text colour cannot read on all three.

## Firefox

The chrome is three stacked rows at the top of the window, all the same height: the
tabs, the navigation buttons with the URL bar, and the bookmarks. No rounded corners or
shadows. Tabs are raised controls with the Chicago bevel, flush in one row, like GTK
notebook tabs: the selected tab takes the accent, the others sit on the surface colour,
hover is the hover colour, a tab that needs attention the urgent colour, a loading tab
the warning colour. The URL bar is a sunken field in the terminal font with the accent
outline when focused; its suggestions hang from it in the same field. Toolbar buttons
center their icons with no hover box: the hover is the icon's ink alone. Popups, the
Library, dialogs and the sidebar are flat and outlined.

Firefox's own pages (settings, add-ons, downloads, the new tab, every `about:` page) are
covered by `userContent.css`, which sets Firefox's in-content variables to the theme's
roles and gives buttons and fields the bevel, and also covers `file://` listings and the
PDF viewer. The GTK file chooser is GTK's: the gtk module themes it.

The profile written to is the one a running Firefox holds, otherwise the one used most
recently; the module prints which, and whether a restart is needed (`userChrome.css` is
read at profile startup only). It needs
`toolkit.legacyUserProfileCustomizations.stylesheets = true` in `about:config`; the
module does not write `user.js`. Profiles are looked for in `~/.config/mozilla/firefox`,
`~/.mozilla/firefox`, the snap and the Flatpak.

**Icons:** the toolbar buttons show icons from the installed icon theme, searched along
its `Inherits` chain, then hicolor, then Adwaita. Icons are embedded in the stylesheet
as `list-style-image`, so Firefox needs no file access. By default each icon is the
theme's own, recoloured to the bar's ink; `style=mask` draws every icon's shape as a CSS
mask over the ink instead, in a fixed box, which evens out an icon theme with mixed
artwork. A glyph missing from every theme in the chain comes from the bundled Adwaita
icons, so every button has an icon whatever the icon theme.

**How the template reaches Firefox's own rules:** Firefox draws the chrome from design
tokens declared on `:root` and, inside its custom elements, on `:host` in shadow trees
that `userChrome.css` cannot reach. The template declares every token on `*` with
`!important`, which wins in both places, under both the names of Firefox 115–140 and
those of the "Nova" design in 158. Checked with `test/firefox.sh` in Firefox 140 (ESR)
and 158.

Limit: a stylesheet cannot move Firefox's page-action buttons into the nav bar; use
Customize Toolbar once to drag them there.

## Thunderbird

Thunderbird (115 and newer) draws its interface from design tokens much like Firefox's.
The set's `thunderbird/tokens.css` sets them all to the theme's roles, on every element,
so the shadow trees of Thunderbird's custom elements follow too. The folder pane,
message list and message header are content documents since 115, so the tokens go into
`userContent.css` as well. Mapping: content and lists on the field colour; panes,
sidebars and windows on `bg`; toolbars, the spaces bar and the message header on the
bar colour; controls raised on `face` with the bevel; the selected folder, message, tab
and space the accent.

The profile is found as for Firefox. It needs
`toolkit.legacyUserProfileCustomizations.stylesheets = true` in the config editor, and
a restart after applying. The selectors and token names are taken from Thunderbird's
own stylesheets; not checked by running Thunderbird here.

## Themes

Each file's header names its sources and says where values are approximate.

| Theme | Source |
|---|---|
| habamax, crimson, desert, slate | neovim-derived |
| catppuccin | Mocha (dark), Latte (light) |
| gruvbox | dark and light |
| rose-pine | main (dark), dawn (light) |
| dracula | Dracula (dark), Alucard (light) |
| nord | dark; the light section is derived |
| tokyonight | night (dark), day (light) |
| solarized | dark and light |
| github | Primer default dark and light (terminal colours approximate) |
| zenbones | zenwritten, dark and light |
| xterm | xterm's own defaults, dark and light |
| greyscale | neutral greys, high contrast |
| suckless | dwm's and dmenu's default colours; ANSI approximate, light derived |

A theme is a `.theme` file with a `[dark]` and a `[light]` section, each giving `bg`,
`fg`, `surf`, `acc`, `accfg`, `urg`, `warn` and `ansi0`–`ansi15`. Drop your own into
`~/.config/themer/themes/`.

## Fonts

The font browser lists every installed family, from `fc-list` when present and from
the name table of each `.ttf`/`.otf`/`.ttc` under the standard font directories. The UI
font goes to GTK, Qt, waybar, sway/i3 and fcitx; the terminal font to foot and the Qt
fixed font. Ctrl-P in the font browser writes an HTML specimen in the theme's colours
to `~/.cache/themer/fonts/` and opens it.

## Writing a module

See `include/themer_module.h` and `modules/example.c`. A module is one C file:

```sh
cc -shared -fPIC -I PREFIX/include/themer -o name.so name.c
```

dropped into `~/.config/themer/modules/`. Modules are compiled against the layout of
`TmModule` and `TmTheme` (`TM_ABI`); one built for another layout is skipped with a
message. A module with a `present()` hook is skipped by apply where its program is
missing. `make install` replaces the installed modules; rebuild any in
`~/.config/themer/modules`.

## Upgrading

- From a build that rendered the theme into `~/.config/gtk-3.0/gtk.css`: the gtk
  module moves that file to `gtk.css.themer-old` on the next apply (a user stylesheet
  outranks the theme, so left there it would keep feeding its colours to programs that
  read the named colours); delete the copy once nothing is missed.
- From a build whose templates were `~/.config/themer/templates/<dir>/<set>/`: the
  layout is `<set>/<dir>/` now; the old directories are not read, delete them. A saved
  set name "Themer Chicago" is read as `themer_chicago`; a saved template option of
  `default` falls back to it.
- From a build with the single `wm` module: `themer module unload wm` with that build
  first. From a build whose waybar module used a marked block: `themer module unload
  waybar` with that build.
- Modules built before TM_ABI 5 must be rebuilt.
- The sway/i3 border, gaps and titlebar options and the qt interface options are gone:
  set those in your own config; a saved value is ignored.

## Status and license

**0.1.** Proven on sway (Wayland) with GTK 3.24 and 4, Qt 5.15 and 6, Firefox 140,
fcitx5, waybar and foot; the i3/X11 paths run in `make check` but have had less time
on a real X session. `set import` is a converter of best effort: an imported theme is a
starting point to edit, not a finished set.

GPL-2.0-only, see [LICENSE](LICENSE). Copyright (c) 2026 Ronald.
