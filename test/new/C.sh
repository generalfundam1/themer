# ---- launchers, notifications, the lock screen: rofi, fuzzel, mako, dunst, swaync,
# swaylock. Self-contained: stand-ins for the programs, the user's own configs, load,
# apply again, the other set and mode, unload and compare byte for byte. ----
t "rofi, fuzzel, mako, dunst, swaync, swaylock: written, idempotent, restored"
for p in rofi fuzzel mako makoctl dunst dunstctl swaync swaync-client swaylock; do
	printf '#!/bin/sh\nexit 0\n' > "$HOME/stubs/$p"; chmod +x "$HOME/stubs/$p"
done
mkdir -p "$HOME/.config/rofi" "$HOME/.config/fuzzel" "$HOME/.config/mako" "$HOME/.config/swaylock" \
         "$HOME/.config/swaync" "$HOME/groupC"
printf 'configuration {\n  modi: "drun,run";\n}\n@theme "gruvbox-dark"\n' > "$HOME/.config/rofi/config.rasi"
printf '[main]\nfont=Mine:size=9\nterminal=foot\n\n[colors]\nbackground=000000ff\n' > "$HOME/.config/fuzzel/fuzzel.ini"
printf 'font=Mine 9\nmax-visible=3\n\n[app-name=spotify]\ninvisible=1\n' > "$HOME/.config/mako/config"
printf 'ignore-empty-password\ncolor=000000\nshow-failed-attempts\n' > "$HOME/.config/swaylock/config"
printf '.notification { margin: 4px; }\n' > "$HOME/.config/swaync/style.css"
for f in rofi/config.rasi fuzzel/fuzzel.ini mako/config swaylock/config swaync/style.css; do
	cp "$HOME/.config/$f" "$HOME/groupC/$(echo "$f" | tr / _)"
done
for m in rofi fuzzel mako dunst swaync swaylock; do
	./themer module load "$m" >/dev/null 2>&1 || bad "load $m"
done
expect .local/share/rofi/themes/themer.rasi
grep -q '^@theme ".*/.local/share/rofi/themes/themer.rasi"$' "$HOME/.config/rofi/config.rasi" || bad "rofi: theme not selected in config.rasi"
grep -q '^@theme "gruvbox-dark"$' "$HOME/.config/rofi/config.rasi" || bad "rofi: the user's own @theme line lost"
grep -q 'selected-normal-background: *#[0-9a-f]\{6\};' "$HOME/.local/share/rofi/themes/themer.rasi" || bad "rofi: no selection color"
grep -q '^terminal=foot$' "$HOME/.config/fuzzel/fuzzel.ini" || bad "fuzzel: the user's key lost"
grep -q '^background=[0-9a-f]\{8\}$' "$HOME/.config/fuzzel/fuzzel.ini" || bad "fuzzel: background not RRGGBBAA"
[ "$(grep -c '^\[colors\]' "$HOME/.config/fuzzel/fuzzel.ini")" = 1 ] || bad "fuzzel: [colors] repeated"
grep -q '^font=.*:size=[0-9]*$' "$HOME/.config/fuzzel/fuzzel.ini" || bad "fuzzel: font not set"
sed -n '1,/^\[/p' "$HOME/.config/mako/config" | grep -q '^background-color=#' || bad "mako: colors not in the global section"
grep -q '^max-visible=3$' "$HOME/.config/mako/config" || bad "mako: the user's key lost"
grep -q '^\[urgency=high\]$' "$HOME/.config/mako/config" || bad "mako: no urgent border"
expect .config/dunst/dunstrc.d/90-themer.conf
expect .config/dunst/dunstrc
grep -q 'frame_color = "#[0-9a-f]\{6\}"' "$HOME/.config/dunst/dunstrc.d/90-themer.conf" || bad "dunst: no frame color"
grep -q '^\.notification { margin: 4px; }$' "$HOME/.config/swaync/style.css" || bad "swaync: the user's rule lost"
grep -q 'themer:swaync begin' "$HOME/.config/swaync/style.css" || bad "swaync: no block"
grep -q -- '--noti-bg: #' "$HOME/.config/swaync/style.css" || bad "swaync: no custom properties"
grep -q '^ignore-empty-password$' "$HOME/.config/swaylock/config" || bad "swaylock: the user's flag lost"
[ "$(grep -c '^color=' "$HOME/.config/swaylock/config")" = 1 ] || bad "swaylock: color repeated"
grep -q '^ring-wrong-color=[0-9a-f]\{6\}$' "$HOME/.config/swaylock/config" || bad "swaylock: no wrong-password ring"
# again: the same bytes
for f in rofi/config.rasi fuzzel/fuzzel.ini mako/config swaylock/config swaync/style.css dunst/dunstrc.d/90-themer.conf; do
	cp "$HOME/.config/$f" "$HOME/groupC/again_$(echo "$f" | tr / _)"
done
./themer apply >/dev/null 2>&1
for f in rofi/config.rasi fuzzel/fuzzel.ini mako/config swaylock/config swaync/style.css dunst/dunstrc.d/90-themer.conf; do
	cmp -s "$HOME/.config/$f" "$HOME/groupC/again_$(echo "$f" | tr / _)" || bad "$f changed on a second apply"
done
# Chicago: square, 1px
./themer set themer_chicago >/dev/null 2>&1
grep -q '^radius=0$' "$HOME/.config/fuzzel/fuzzel.ini" || bad "fuzzel: Chicago is not square"
grep -q '^border-size=1$' "$HOME/.config/mako/config" || bad "mako: Chicago's border is not 1px"
grep -q 'frame_width = 1' "$HOME/.config/dunst/dunstrc.d/90-themer.conf" || bad "dunst: Chicago's frame is not 1px"
grep -q 'border-radius: 0;' "$HOME/.config/swaync/style.css" || bad "swaync: Chicago is not square"
grep -q 'border: *1px solid 0px 0px;' "$HOME/.local/share/rofi/themes/themer.rasi" || bad "rofi: Chicago's separators are not solid"
./themer set themer_default >/dev/null 2>&1
grep -q '^radius=12$' "$HOME/.config/fuzzel/fuzzel.ini" || bad "fuzzel: back to Themer Default"
for m in rofi fuzzel mako dunst swaync swaylock; do
	./themer module unload "$m" >/dev/null 2>&1 || bad "unload $m"
done
for f in rofi/config.rasi fuzzel/fuzzel.ini mako/config swaylock/config swaync/style.css; do
	cmp -s "$HOME/.config/$f" "$HOME/groupC/$(echo "$f" | tr / _)" || bad "$f not restored"
done
[ -e "$HOME/.config/dunst" ] && bad "dunst: files left after unload"
[ -e "$HOME/.local/share/rofi" ] && bad "rofi: theme left after unload"
# swaync without a style.css of the user's: themer's imports the system's first, and goes
rm "$HOME/.config/swaync/style.css"
./themer module load swaync >/dev/null 2>&1
if [ -r /etc/xdg/swaync/style.css ]; then
	head -4 "$HOME/.config/swaync/style.css" | grep -q '@import url("file:///etc/xdg/swaync/style.css");' ||
		bad "swaync: the system's style not imported first"
fi
./themer module unload swaync >/dev/null 2>&1
[ -e "$HOME/.config/swaync/style.css" ] && bad "swaync: the created style.css left behind"
cp "$HOME/groupC/swaync_style.css" "$HOME/.config/swaync/style.css"
rm -rf "$HOME/groupC"
