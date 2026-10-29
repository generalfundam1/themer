<!-- group E: helix, vscode, kde. Rows for the module table, rows for the options
     table, and a paragraph per module for the module notes. -->

Module table rows:

| **helix** | Helix: a `themer` theme in `~/.config/helix/themes/themer.toml` (the interface in the theme's roles, the syntax in its terminal colors), selected with `theme = "themer"` in `config.toml`: your theme line changed in place, or a marked block at the top when you have none |
| **vscode** | VS Code, VSCodium and Code - OSS (and their Flatpaks): VS Code's own Default Dark or Light Modern, recolored, through four top-level keys of each one's `User/settings.json`; no extension is installed |
| **kde** | KDE programs (Dolphin, Kate, Okular, KWin's title bars): the theme as a KDE color scheme, `~/.local/share/color-schemes/Themer.colors`, and the same keys set in `~/.config/kdeglobals` with the fonts |

Options table rows:

| vscode | `font` | `own` (VS Code's) \| `terminal` (the editor's and its terminal's) | `own` |
| kde | `font` | `ui` \| `terminal` (menus, toolbars, windows; the fixed font is always the terminal font) | `ui` |

Module notes:

**helix**. Writes `~/.config/helix/themes/themer.toml` from `templates/<set>/helix/themer.toml`. Helix's theme keys come from a `[palette]` of the theme's roles and its terminal colors: `ui.*` in the roles (the status line and the selected menu item in the accent, popups on the surface color, the selection in the `control_active` tone), the syntax in ansi1–ansi6, comments in the `disabled` tone. In `~/.config/helix/config.toml` your `theme =` line becomes `theme = "themer"` and comes back on unload; without one, a marked block at the top of the file holds it, since a top-level key must come before any table. A running Helix takes it with `:config-reload`. Loadable when `hx` or `helix` is on PATH, or the Flatpak `com.helix_editor.Helix` is installed.

**vscode**. Sets four top-level keys in `User/settings.json` with `tm_json_set`, which leaves the rest of the file (comments included) as it is and takes them out again, or puts your values back, on unload: `workbench.colorTheme` (Default Dark Modern or Default Light Modern, by the mode), `workbench.colorCustomizations` (`templates/<set>/vscode/colors.json`: the workbench in the roles and Adwaita tones, the terminal in the theme's colors), `editor.tokenColorCustomizations` (`tokens.json`: the syntax in the terminal colors), and with `font=terminal` the editor's and the integrated terminal's font. The editor and the chrome around it keep the Modern themes' relation: on the dark side the chrome a step darker than the editor, on the light side the editor white. VS Code watches its settings, so a running one changes at once. Settings directories: `~/.config/Code` for `code` (and `~/.config/Code - OSS`, where Arch's `code` keeps them, when it is there), `~/.config/Code - OSS` for `code-oss`, `~/.config/VSCodium` for `codium`, and `~/.var/app/com.visualstudio.code/config/Code` and `~/.var/app/com.vscodium.codium/config/VSCodium` for the Flatpaks. A missing `settings.json` is created holding only these. While the module is on, your own `workbench.colorCustomizations` is replaced by the theme's.

**kde**. KDE programs take their palette from `kdeglobals`. The module writes the theme as a full KDE color scheme to `~/.local/share/color-schemes/Themer.colors` (so System Settings lists it) and sets the same keys in `~/.config/kdeglobals` with `tm_ini`, each restored on unload: the Window, View, Button, Selection, Tooltip, Complementary and Header sets (views in the `view` tone, buttons and headers in `raised`, selection in the accent; links in ansi4, visited ansi5, negative/neutral/positive text in the urgent color, the warning color and ansi2, each moved until it reads 4.5:1), KWin's title bar colors under `[WM]`, `[General] ColorScheme=Themer`, the fonts (`font`, `menuFont`, `toolBarFont`, `smallestReadableFont`, `fixed`, `[WM] activeFont`) and the icon theme. Plasma tells running programs; outside Plasma, restart them. Loadable when KDE Frameworks are installed (`libKF6ConfigCore` or `libKF5ConfigCore`, or `kreadconfig6`/`kreadconfig5`).
