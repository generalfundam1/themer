# Drives a running Firefox (started with --marionette -remote-allow-system-access) and
# writes screenshots of the themed chrome to the directory given: idle, the URL bar
# focused with suggestions, a hovered tab, and about:preferences. Prints the toolbar
# geometry, so the rows and the icon centering can be checked by number.
import json, sys, time
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from marionette import M

out = sys.argv[1]
m = M(); m.chrome(); time.sleep(2)
m.js('''const g = window.gBrowser, p = Services.scriptSecurityManager.getSystemPrincipal();
g.addTab("about:blank", {triggeringPrincipal: p}); g.addTab("about:blank", {triggeringPrincipal: p});
const {PlacesUtils} = ChromeUtils.importESModule("resource://gre/modules/PlacesUtils.sys.mjs");
PlacesUtils.bookmarks.insert({parentGuid: PlacesUtils.bookmarks.toolbarGuid, url: "https://example.org/", title: "Example"});''')
time.sleep(1.5)
print(m.js('''const d = window.document;
const r = s => { const e = d.querySelector(s); if (!e) return null; const b = e.getBoundingClientRect(); return [Math.round(b.x), Math.round(b.y), Math.round(b.width), Math.round(b.height)]; };
return JSON.stringify({tabs: r('#TabsToolbar'), nav: r('#nav-bar'), bookmarks: r('#PersonalToolbar'), urlbar: r('#urlbar'),
  back: r('#back-button'), back_icon: r('#back-button > .toolbarbutton-icon'), menu: r('#PanelUI-menu-button'),
  menu_icon: r('#PanelUI-menu-button .toolbarbutton-icon'), tab: r('.tabbrowser-tab')});'''))
m.shot(out + '/idle.png')
m.js('window.gURLBar.focus(); window.gURLBar.value = "exam"; window.gURLBar.startQuery();'); time.sleep(2)
m.shot(out + '/urlbar.png')
m.js('window.gURLBar.view.close(); window.gURLBar.blur();')
m.js('''const t = window.document.querySelectorAll('.tabbrowser-tab')[1], b = t.getBoundingClientRect();
window.windowUtils.sendMouseEvent("mousemove", b.x + b.width / 2, b.y + b.height / 2, 0, 0, 0);'''); time.sleep(0.7)
m.shot(out + '/hover.png')
m.js('window.openTrustedLinkIn("about:preferences", "current");'); time.sleep(3)
m.shot(out + '/preferences.png')
