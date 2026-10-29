<!-- group B: the terminals. Rows for the README's module table, the options table,
     and a paragraph per module. -->

### Module table rows

| **kitty** | colors, font and tab bar in `~/.config/kitty/themer.conf`, included from the end of `kitty.conf`; running kittys reload (SIGUSR1) |
| **alacritty** | colors and font in `~/.config/alacritty/themer.toml`, added to `general.import` in `alacritty.toml` (Alacritty 0.14+) |
| **ghostty** | colors and font in `~/.config/ghostty/themer`, read with `config-file = ?themer` from the end of Ghostty's config; Ghostty 1.2+ reloads (SIGUSR2) |
| **wezterm** | a `themer` color scheme in `~/.config/themer/wezterm/`, selected (with the font and window frame) from `wezterm.lua` |
| **tmux** | the status line, pane borders, messages, menus and copy mode in `~/.config/themer/tmux/themer.conf`, sourced from the end of `~/.tmux.conf` (or `~/.config/tmux/tmux.conf`); a running server re-reads it |

### Options table rows

| kitty, alacritty, ghostty, wezterm | `font` | `terminal` \| `ui` | `terminal` |

(tmux has no font; all five have `mode` and `template`.)

### Terminals

**kitty** gets `themer.conf` next to `kitty.conf`, and a marked block at the end of
`kitty.conf` (created when there is none) that includes it, so the theme is read
after your own settings: the sixteen colors, background, foreground, cursor,
selection, URL and window-border colors, the tab bar, and the font. Themer Default
keeps kitty's own tab bar in the theme's colors, the focused tab in the accent;
Themer Chicago draws flat separated tabs on a grey bar. Running kittys re-read their
config (SIGUSR1) after every apply.

**alacritty** writes `themer.toml` next to `alacritty.toml` and imports it
(`general.import`, Alacritty 0.14 and later). TOML allows a table once and wants the
root's keys before any table, so the import goes where your file already has one:
added to your `import` line (last in a one-line list, so the theme wins over your other
imports; first in a list spread over lines; the line is put back on unload), under
your `[general]` table in a marked block, or as `general.import` in a block at the top
of the file when it has neither (the file is created when there is none). Alacritty
reads imports before the file itself, so colors you set in `alacritty.toml` still win
over the theme's: take them out to follow the theme. Alacritty watches its config and
its imports, so there is nothing to reload.

**ghostty** writes `~/.config/ghostty/themer` and reads it with `config-file =
?themer` from a block at the end of `config` (or `config.ghostty` when that is the only
one; `config` is created when there is neither). Ghostty reads a `config-file` after
the file that names it, and `config` after `config.ghostty`, so the theme wins over the
same keys in your config. The font replaces your `font-family` list instead of
joining it. Ghostty 1.2 and later reload on SIGUSR2; an older Ghostty would quit on it,
so the module asks for the version first and leaves those alone.

**wezterm** writes a color scheme, `themer`, to `~/.config/themer/wezterm/themer.toml`
and selects it from `wezterm.lua` (`~/.wezterm.lua` when it exists, else
`~/.config/wezterm/wezterm.lua`): a marked block right after the line that makes the
config table (`local config = wezterm.config_builder()` or `local config = {}`) adds the
scheme's directory, picks the scheme, and sets the font and the window frame. Lines
further down your file still win, so a `config.color_scheme = ...` of your own keeps
your scheme. Without a `wezterm.lua` the block is a whole config of its own, which
unload takes away again. A `wezterm.lua` of another shape (one that returns a table
built in place) is left alone: the scheme is written, and a message gives the two
settings to add. The block names the theme and its main colors, so the file changes
with the theme and WezTerm, which watches its config, reloads. Themer Chicago turns the
fancy tab bar off for WezTerm's plain one, grey with the focused tab in the accent.

**tmux** writes `~/.config/themer/tmux/themer.conf` and sources it (`source-file -q`)
from a block at the end of `~/.tmux.conf`, or of `~/.config/tmux/tmux.conf` when only
that one exists (`~/.tmux.conf` is created when there is neither). Themer Default keeps
tmux's look: the status line in the accent where tmux's is green, messages in the
warning color where tmux's are yellow, the active pane border, copy-mode selections
and menus in the accent. Themer Chicago draws the status line as a grey taskbar with
the current window raised in the accent. Every option is set with `set -gq`, so an
older tmux skips the ones it does not have. A running server sources the file again
after every apply; options the theme stops setting keep their value until the server
restarts.
