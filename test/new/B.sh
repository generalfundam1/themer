# ---- the terminals: kitty, alacritty, ghostty, wezterm, tmux ----
# Self-contained: stand-in programs on PATH (the modules load only where their
# program is installed), seeded configs, load, apply again, both sets and modes,
# then unload gives every file back byte for byte.
t "terminals: kitty, alacritty, ghostty, wezterm, tmux"
stubs="$HOME/stubs-terminals"; mkdir -p "$stubs"
for p in kitty alacritty ghostty wezterm tmux; do
	printf '#!/bin/sh\nexit 0\n' > "$stubs/$p"; chmod +x "$stubs/$p"
done
oldpath=$PATH; PATH="$stubs:$PATH"
mkdir -p "$HOME/.config/kitty" "$HOME/.config/alacritty" "$HOME/.config/ghostty" "$HOME/.config/wezterm"
printf '# mine\nfont_size 9\n' > "$HOME/.config/kitty/kitty.conf"
printf '[general]\nimport = ["~/.config/alacritty/mine.toml"]\n\n[font]\nsize = 9\n' > "$HOME/.config/alacritty/alacritty.toml"
printf 'font-size = 9\n' > "$HOME/.config/ghostty/config"
printf "local wezterm = require 'wezterm'\nlocal config = wezterm.config_builder()\nconfig.font_size = 9\nreturn config\n" > "$HOME/.config/wezterm/wezterm.lua"
printf 'set -g mouse on\n' > "$HOME/.tmux.conf"
mkdir -p "$HOME/orig-terminals" && cp -r "$HOME/.config/kitty" "$HOME/.config/alacritty" "$HOME/.config/ghostty" \
	"$HOME/.config/wezterm" "$HOME/.tmux.conf" "$HOME/orig-terminals/"
for m in kitty alacritty ghostty wezterm tmux; do
	./themer module load $m >/dev/null 2>&1 || bad "$m did not load"
done
./themer apply >/dev/null 2>&1
grep -q '^include themer.conf$' "$HOME/.config/kitty/kitty.conf" || bad "kitty: themer.conf not included"
grep -q '^color15 #' "$HOME/.config/kitty/themer.conf" || bad "kitty: palette not written"
grep -q '^import = \["~/.config/alacritty/mine.toml", "~/.config/alacritty/themer.toml"\]$' "$HOME/.config/alacritty/alacritty.toml" ||
	bad "alacritty: themer.toml not added to the user's imports"
grep -q '^\[colors.bright\]' "$HOME/.config/alacritty/themer.toml" || bad "alacritty: colors not written"
grep -q '^config-file = ?themer$' "$HOME/.config/ghostty/config" || bad "ghostty: themer not read from config"
grep -q '^palette = 15=#' "$HOME/.config/ghostty/themer" || bad "ghostty: palette not written"
sed -n '3p' "$HOME/.config/wezterm/wezterm.lua" | grep -q 'themer:wezterm begin' || bad "wezterm: the block is not right after local config"
grep -q '^name = "themer"' "$HOME/.config/themer/wezterm/themer.toml" || bad "wezterm: scheme not written"
grep -q 'source-file -q ~/.config/themer/tmux/themer.conf' "$HOME/.tmux.conf" || bad "tmux: theme not sourced"
grep -q '^set -gq status-style "bg=#' "$HOME/.config/themer/tmux/themer.conf" || bad "tmux: status line not themed"
[ "$(grep -c 'themer:kitty begin' "$HOME/.config/kitty/kitty.conf")" = 1 ] || bad "kitty: block repeated by apply"
[ "$(grep -c 'themer.toml' "$HOME/.config/alacritty/alacritty.toml")" = 1 ] || bad "alacritty: import repeated by apply"
# the files parse: TOML with python's tomllib, tmux with tmux itself when installed
if command -v python3 >/dev/null 2>&1; then
	for f in .config/alacritty/alacritty.toml .config/alacritty/themer.toml .config/themer/wezterm/themer.toml; do
		python3 -c "import tomllib, sys; tomllib.load(open(sys.argv[1], 'rb'))" "$HOME/$f" 2>/dev/null || bad "$f is not valid TOML"
	done
fi
./themer set themer_chicago >/dev/null 2>&1
grep -q '^tab_bar_style separator' "$HOME/.config/kitty/themer.conf" || bad "kitty: Chicago's tab bar not rendered"
grep -q 'use_fancy_tab_bar = false' "$HOME/.config/wezterm/wezterm.lua" || bad "wezterm: Chicago's plain tab bar not selected"
PATH=$oldpath
if command -v tmux >/dev/null 2>&1; then
	tmux -L themercheck -f /dev/null new-session -d 2>/dev/null &&
		{ tmux -L themercheck source-file "$HOME/.config/themer/tmux/themer.conf" 2>/dev/null || bad "tmux: themer.conf has errors"; }
	tmux -L themercheck kill-server 2>/dev/null
fi
PATH="$stubs:$PATH"
./themer light >/dev/null 2>&1; ./themer dark >/dev/null 2>&1; ./themer set themer_default >/dev/null 2>&1
for m in kitty alacritty ghostty wezterm tmux; do ./themer module unload $m >/dev/null 2>&1; done
for f in kitty/kitty.conf alacritty/alacritty.toml ghostty/config wezterm/wezterm.lua; do
	cmp -s "$HOME/orig-terminals/${f%%/*}/${f#*/}" "$HOME/.config/$f" || bad "unload did not restore .config/$f"
done
cmp -s "$HOME/orig-terminals/.tmux.conf" "$HOME/.tmux.conf" || bad "unload did not restore .tmux.conf"
for f in .config/kitty/themer.conf .config/alacritty/themer.toml .config/ghostty/themer .config/themer/wezterm .config/themer/tmux; do
	[ -e "$HOME/$f" ] && bad "unload left $f"
done
rm -rf "$HOME/.config/kitty" "$HOME/.config/alacritty" "$HOME/.config/ghostty" "$HOME/.config/wezterm" "$HOME/.tmux.conf" "$HOME/orig-terminals"
PATH=$oldpath
