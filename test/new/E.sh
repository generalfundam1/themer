# ---- helix, vscode, kde: each with its program stubbed on PATH ----
t "helix, vscode, kde: the theme in, your files back"
mkdir -p "$HOME/stubE" "$HOME/.config/helix" "$HOME/.config/Code/User"
for p in hx code kreadconfig6; do printf '#!/bin/sh\n' > "$HOME/stubE/$p"; chmod +x "$HOME/stubE/$p"; done
printf 'theme = "onedark"\n\n[editor]\nline-number = "relative"\n' > "$HOME/.config/helix/config.toml"
printf '{\n    // mine\n    "editor.fontSize": 13,\n    "workbench.colorTheme": "Monokai"\n}\n' > "$HOME/.config/Code/User/settings.json"
printf '[General]\nfont=Noto Sans,10,-1,5,50,0,0,0,0,0\n\n[KDE]\nSingleClick=false\n' > "$HOME/.config/kdeglobals"
cp "$HOME/.config/helix/config.toml" "$HOME/helix.orig"
cp "$HOME/.config/Code/User/settings.json" "$HOME/code.orig"
cp "$HOME/.config/kdeglobals" "$HOME/kde.orig"
# without their programs they are not loadable
for m in helix vscode kde; do
	./themer module load $m >/dev/null 2>&1 && bad "$m loaded without its program"
done
for m in helix vscode kde; do
	PATH="$HOME/stubE:$PATH" ./themer module load $m >/dev/null 2>&1 || bad "$m did not load"
done
PATH="$HOME/stubE:$PATH" ./themer apply >/dev/null 2>&1
expect .config/helix/themes/themer.toml
grep -qx 'theme = "themer"' "$HOME/.config/helix/config.toml" || bad "helix: theme not selected"
grep -q 'theme = "onedark"' "$HOME/.config/helix/config.toml" && bad "helix: two theme lines"
grep -q '^"ui.background" = { bg = "bg" }' "$HOME/.config/helix/themes/themer.toml" || bad "helix: theme not rendered"
grep -q '"workbench.colorTheme": "Default \(Dark\|Light\) Modern"' "$HOME/.config/Code/User/settings.json" || bad "vscode: theme not set"
grep -q '"workbench.colorCustomizations": {' "$HOME/.config/Code/User/settings.json" || bad "vscode: colors not set"
grep -q '// mine' "$HOME/.config/Code/User/settings.json" || bad "vscode: the user's comment lost"
grep -q '"editor.fontSize": 13' "$HOME/.config/Code/User/settings.json" || bad "vscode: the user's setting lost"
grep -q 'editor.fontFamily' "$HOME/.config/Code/User/settings.json" && bad "vscode: font=own set a font"
if command -v python3 >/dev/null 2>&1; then
	python3 -c 'import tomllib,sys; tomllib.load(open(sys.argv[1],"rb")); tomllib.load(open(sys.argv[2],"rb"))' \
		"$HOME/.config/helix/themes/themer.toml" "$HOME/.config/helix/config.toml" 2>/dev/null || bad "helix: not valid TOML"
	python3 -c 'import json,re,sys; json.loads(re.sub(r"(?m)^\s*//.*$","",open(sys.argv[1]).read()))' \
		"$HOME/.config/Code/User/settings.json" 2>/dev/null || bad "vscode: settings.json not valid JSON"
fi
expect .local/share/color-schemes/Themer.colors
grep -qx 'ColorScheme=Themer' "$HOME/.config/kdeglobals" || bad "kde: scheme not selected"
grep -q '^\[Colors:View\]' "$HOME/.config/kdeglobals" || bad "kde: no view colors"
grep -q '^BackgroundNormal=[0-9]*,[0-9]*,[0-9]*$' "$HOME/.config/kdeglobals" || bad "kde: colors not r,g,b"
grep -qx 'SingleClick=false' "$HOME/.config/kdeglobals" || bad "kde: the user's key lost"
PATH="$HOME/stubE:$PATH" ./themer module option vscode font terminal >/dev/null 2>&1
grep -q '"editor.fontFamily": "' "$HOME/.config/Code/User/settings.json" || bad "vscode: font=terminal not set"
PATH="$HOME/stubE:$PATH" ./themer module reset vscode font >/dev/null 2>&1
for m in helix vscode kde; do
	PATH="$HOME/stubE:$PATH" ./themer module unload $m >/dev/null 2>&1
done
cmp -s "$HOME/.config/helix/config.toml" "$HOME/helix.orig" || bad "helix: config.toml not restored"
cmp -s "$HOME/.config/Code/User/settings.json" "$HOME/code.orig" || bad "vscode: settings.json not restored"
cmp -s "$HOME/.config/kdeglobals" "$HOME/kde.orig" || bad "kde: kdeglobals not restored"
[ -e "$HOME/.config/helix/themes/themer.toml" ] && bad "helix: theme file left behind"
[ -e "$HOME/.local/share/color-schemes/Themer.colors" ] && bad "kde: scheme file left behind"
rm -rf "$HOME/stubE" "$HOME/helix.orig" "$HOME/code.orig" "$HOME/kde.orig" "$HOME/.config/helix" "$HOME/.config/Code" "$HOME/.config/kdeglobals"
