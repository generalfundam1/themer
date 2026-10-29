<!-- group D: command-line tools. Rows for the module table ("Module | Themes"),
     rows for the options table ("Module | Option | Values | Default"), and a
     paragraph per module for a section on the tools. -->

### Module table rows

| **btop** | the system monitor's colors as a theme of its own, `~/.config/btop/themes/themer.theme`, selected by `color_theme` in `btop.conf` (changed in place; Themer Chicago also squares the boxes) |
| **zathura** | the document viewer's colors and font, a marked block at the end of `zathurarc` |
| **bat** | syntax colors from the terminal's palette: `--theme="ansi"` (or `base16`) at the end of bat's `config` |
| **lazygit** | the git interface's colors, `~/.config/themer/lazygit/config.yml`, merged over yours through `LG_CONFIG_FILE` (environment.d) |
| **fzf** | the fuzzy finder's colors, `~/.config/themer/fzf/fzfrc`, through `FZF_DEFAULT_OPTS_FILE` (environment.d; fzf 0.47+) |
| **vivid** | `LS_COLORS` for ls, fd, eza, tree and completion: a vivid theme, `~/.config/vivid/themes/themer.yml`, and its output in environment.d |
| **starship** | the prompt's colors: a `themer` palette in `starship.toml` that defines every color name as the theme's, and `palette = "themer"` |

### Options table rows

| zathura | `font` | `terminal` (zathura's own default is monospace) \| `ui` | `terminal` |
| bat | `theme` | `ansi` (the terminal's eight colors) \| `base16` (all sixteen) | `ansi` |

btop, lazygit, fzf, vivid and starship have no options of their own beyond `mode` and
`template`; bat has no `mode` (its colors are the terminal's).

### The command-line tools

Each keeps its own look, in the theme's colors: the hues each tool uses for its parts
come from the theme's terminal colors, the background and text from the theme's, and
selections and the focused frame are the accent, as everywhere. All of them need the
program itself installed.

**btop** reads themes from `~/.config/btop/themes/`; themer writes `themer.theme`
there (btop's default theme, hue for hue: boxes, meters and graphs in the terminal
colors, gradients fading in from the background as btop's do) and sets
`color_theme = "themer"` in `~/.config/btop/btop.conf`. btop rewrites `btop.conf`
itself whenever a setting changes, keeping each line where it is, so the line is
changed in place rather than in a marked block, and put back on unload. A set may
change more lines the same way in `templates/<set>/btop/settings`: Themer Chicago sets
`rounded_corners = False`. Before btop's first run there is no `btop.conf`; the lines
then go in a marked block of a new file, which btop fills out on its next exit. A
running btop takes the theme when restarted.

**zathura**: a marked block of `set` lines at the end of `~/.config/zathura/zathurarc`,
read last, so it overrides the same settings above it. The page area and status bar
sit on the background, the input bar and completion on the surface, search hits in a
see-through accent. Pages keep their own colors unless recolor mode is on (Ctrl-R):
then they are drawn in the theme's text and background (`recolor-lightcolor`,
`recolor-darkcolor`). `:source` in a running zathura reads it again.

**bat** has no recolorable default theme (its Monokai Extended is fixed colors), so
the module selects one of bat's own themes that draws in the terminal's palette:
`ansi` (the eight colors) or `base16` (the sixteen), as `--theme=` at the end of
`~/.config/bat/config` (or `$BAT_CONFIG_PATH`), after your own options. The terminal
modules set that palette from the theme, so bat follows every theme and both modes
with nothing generated; a `.tmTheme` in fixed colors would need `bat cache --build`
after every apply, and would be wrong in a terminal themer does not reach.

**lazygit** merges a list of config files, later ones over earlier ones, when
`LG_CONFIG_FILE` names them. themer writes the theme's `gui.theme` keys to
`~/.config/themer/lazygit/config.yml` and sets
`LG_CONFIG_FILE=<your config.yml>,<that file>` in
`~/.config/environment.d/40-themer-lazygit.conf`. lazygit will not start when a listed
file is missing, so when you have no `~/.config/lazygit/config.yml` (or the older
`~/.config/jesseduffield/lazygit/config.yml`) themer creates one holding only an empty
marked block, and removes it on unload if nothing else was written to it.

**fzf** (0.47 and later) reads `$FZF_DEFAULT_OPTS_FILE` before `$FZF_DEFAULT_OPTS`;
themer writes `--color` options to `~/.config/themer/fzf/fzfrc` and sets the variable
in `~/.config/environment.d/40-themer-fzf.conf`. fzf's text and background stay the
terminal's, as in fzf's own scheme; the current line is the surface; matches, prompt
and markers keep fzf's hues. Options of your own in `FZF_DEFAULT_OPTS` still win.

**vivid** generates `LS_COLORS`. themer writes `~/.config/vivid/themes/themer.yml`
(vivid's default theme, molokai, in the theme's colors), runs `vivid generate` on it,
and puts the result in `~/.config/environment.d/40-themer-lscolors.conf`. A shell that
sets `LS_COLORS` itself (`eval "$(dircolors)"`, `LS_COLORS="$(vivid generate molokai)"`)
overrides the session's; write `vivid generate themer` there instead.

**starship**'s modules name their colors (`bold green`, `purple`, `bright-black`), and
starship looks a name up in the selected palette before the terminal's. themer adds a
`[palettes.themer]` table that defines all sixteen names as the theme's terminal colors
(a marked block at the end of `~/.config/starship.toml`, or `$STARSHIP_CONFIG`) and
selects it with `palette = "themer"`. That is a top-level key, which TOML allows only
before the first table: your own `palette = ...` line is changed in place (and put
back on unload), and without one the line goes in a marked block at the very top. The
prompt's layout and symbols stay yours.

Variables set through environment.d reach programs started from the session after the
next login.
