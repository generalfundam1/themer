# ---- the command-line tools: btop, zathura, bat, lazygit, fzf, vivid, starship ----
# Self-contained: stand-ins for the programs, the user's own files, then load,
# re-apply, theme and set changes, and unload putting every file back.
t "tools: btop, zathura, bat, lazygit, fzf, vivid, starship"
for p in btop zathura bat lazygit fzf starship; do
	printf '#!/bin/sh\nexit 0\n' > "$HOME/stubs/$p"; chmod +x "$HOME/stubs/$p"
done
# a stand-in vivid: checks the theme file it is given, prints an LS_COLORS from it
cat > "$HOME/stubs/vivid" <<'V'
#!/bin/sh
[ "$1" = generate ] && [ -f "$2" ] || exit 1
echo "di=38;2;$(sed -n 's/^  cyan: "\(.*\)"/\1/p' "$2"):ln=1"
V
chmod +x "$HOME/stubs/vivid"
mkdir -p "$HOME/.config/btop" "$HOME/.config/zathura" "$HOME/.config/bat" "$HOME/.config/lazygit" "$HOME/tools-orig"
printf '#? Config file for btop v. 1.4.5\ncolor_theme = "Default"\nrounded_corners = True\n' > "$HOME/.config/btop/btop.conf"
printf 'set selection-clipboard clipboard\nset default-bg "#000000"\n' > "$HOME/.config/zathura/zathurarc"
printf -- '--style="numbers,changes"\n--theme="TwoDark"\n' > "$HOME/.config/bat/config"
printf 'gui:\n  showIcons: true\n' > "$HOME/.config/lazygit/config.yml"
printf 'add_newline = false\npalette = "gruvbox"\n\n[character]\nsuccess_symbol = "[>](bold green)"\n\n[palettes.gruvbox]\ngreen = "#00ff00"\n' > "$HOME/.config/starship.toml"
for f in btop/btop.conf zathura/zathurarc bat/config lazygit/config.yml starship.toml; do
	cp "$HOME/.config/$f" "$HOME/tools-orig/$(echo "$f" | tr / _)"
done
for m in btop zathura bat lazygit fzf vivid starship; do
	./themer module load "$m" >/dev/null 2>&1 || bad "$m did not load"
done
grep -q '^color_theme = "themer"' "$HOME/.config/btop/btop.conf" || bad "btop: color_theme not changed in place"
grep -q 'theme\[main_bg\]="#' "$HOME/.config/btop/themes/themer.theme" || bad "btop: no theme file"
tail -1 "$HOME/.config/zathura/zathurarc" | grep -q 'themer:zathura end' || bad "zathura: the block is not last"
grep -q '^set highlight-color "rgba(' "$HOME/.config/zathura/zathurarc" || bad "zathura: highlight not rgba()"
tail -2 "$HOME/.config/bat/config" | grep -q -- '--theme="ansi"' || bad "bat: --theme not last"
grep -q "^LG_CONFIG_FILE=$HOME/.config/lazygit/config.yml,$HOME/.config/themer/lazygit/config.yml\$" \
	"$HOME/.config/environment.d/40-themer-lazygit.conf" || bad "lazygit: LG_CONFIG_FILE"
grep -q "^FZF_DEFAULT_OPTS_FILE=$HOME/.config/themer/fzf/fzfrc\$" "$HOME/.config/environment.d/40-themer-fzf.conf" ||
	bad "fzf: FZF_DEFAULT_OPTS_FILE"
grep -q -- '--color=pointer:#' "$HOME/.config/themer/fzf/fzfrc" || bad "fzf: no colors"
grep -q '^LS_COLORS=di=38;2;[0-9a-f]\{6\}:ln=1$' "$HOME/.config/environment.d/40-themer-lscolors.conf" ||
	bad "vivid: LS_COLORS not from vivid's output"
grep -q '^palette = "themer"$' "$HOME/.config/starship.toml" || bad "starship: palette not selected in place"
if command -v python3 >/dev/null 2>&1; then
	python3 -c "import tomllib,sys; d=tomllib.load(open(sys.argv[1],'rb')); assert d['palette']=='themer' and d['palettes']['themer']['green'].startswith('#') and d['add_newline'] is False" \
		"$HOME/.config/starship.toml" 2>/dev/null || bad "starship: starship.toml is not valid TOML"
fi
cp "$HOME/.config/starship.toml" "$HOME/tools-s1"
./themer apply >/dev/null 2>&1
cmp -s "$HOME/tools-s1" "$HOME/.config/starship.toml" || bad "starship: a second apply changed starship.toml"
[ "$(grep -c 'themer:zathura begin' "$HOME/.config/zathura/zathurarc")" = 1 ] || bad "zathura: the block twice"
./themer set themer_chicago >/dev/null 2>&1
grep -q '^rounded_corners = False' "$HOME/.config/btop/btop.conf" || bad "btop: Chicago's boxes not square"
./themer set themer_default >/dev/null 2>&1
grep -q '^rounded_corners = True' "$HOME/.config/btop/btop.conf" || bad "btop: Themer Default did not give the corners back"
# no btop.conf yet (btop writes it on its first run): a block, gone again on unload
./themer module unload btop >/dev/null 2>&1; mv "$HOME/.config/btop/btop.conf" "$HOME/tools-btop"
./themer module load btop >/dev/null 2>&1
grep -q 'color_theme = "themer"' "$HOME/.config/btop/btop.conf" || bad "btop: no color_theme without a btop.conf"
./themer module unload btop >/dev/null 2>&1
[ -e "$HOME/.config/btop/btop.conf" ] && bad "btop: the btop.conf themer made was left behind"
mv "$HOME/tools-btop" "$HOME/.config/btop/btop.conf"
# lazygit refuses a missing listed file: one is made when there is none, and taken away
./themer module unload lazygit >/dev/null 2>&1; mv "$HOME/.config/lazygit/config.yml" "$HOME/tools-lg"
./themer module load lazygit >/dev/null 2>&1
[ -e "$HOME/.config/lazygit/config.yml" ] || bad "lazygit: no config.yml for LG_CONFIG_FILE to list"
./themer module unload lazygit >/dev/null 2>&1
[ -e "$HOME/.config/lazygit/config.yml" ] && bad "lazygit: the config.yml themer made was left behind"
mv "$HOME/tools-lg" "$HOME/.config/lazygit/config.yml"
# starship with no palette line: one at the very top, before the tables
./themer module unload starship >/dev/null 2>&1
printf '[character]\nsuccess_symbol = "[>](bold green)"\n' > "$HOME/.config/starship.toml"; cp "$HOME/.config/starship.toml" "$HOME/tools-s2"
./themer module load starship >/dev/null 2>&1
head -2 "$HOME/.config/starship.toml" | grep -q '^palette = "themer"$' || bad "starship: palette line not at the top"
./themer module unload starship >/dev/null 2>&1
cmp -s "$HOME/tools-s2" "$HOME/.config/starship.toml" || bad "starship: not restored (no palette line)"
cp "$HOME/tools-orig/starship.toml" "$HOME/.config/starship.toml"
./themer module unload btop >/dev/null 2>&1
for m in btop zathura bat lazygit fzf vivid starship; do ./themer module unload "$m" >/dev/null 2>&1; done
for f in btop/btop.conf zathura/zathurarc bat/config lazygit/config.yml starship.toml; do
	cmp -s "$HOME/tools-orig/$(echo "$f" | tr / _)" "$HOME/.config/$f" || bad "$f not restored"
done
for f in .config/environment.d/40-themer-lazygit.conf .config/environment.d/40-themer-fzf.conf \
         .config/environment.d/40-themer-lscolors.conf .config/btop/themes/themer.theme \
         .config/vivid/themes/themer.yml .config/themer/fzf/fzfrc .config/themer/lazygit/config.yml; do
	[ -e "$HOME/$f" ] && bad "$f left behind"
done
rm -f "$HOME/stubs/btop" "$HOME/stubs/zathura" "$HOME/stubs/bat" "$HOME/stubs/lazygit" "$HOME/stubs/fzf" \
      "$HOME/stubs/vivid" "$HOME/stubs/starship"
