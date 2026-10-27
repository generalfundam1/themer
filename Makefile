# themer - suckless-style build
#
#   src/        the host: the themer binary (CLI and TUI)
#   gui/        themer-gtk and themer-qt, graphical front ends, and gui/qpa, the Qt
#               platform theme plugin; each is built only when pkg-config finds its
#               toolkit (see below)
#   include/    themer_module.h, the API modules are built against
#   modules/    one .c per module, built to a .so each; shared code in .h files
#   templates/  the files modules render, templates/<set>/<dir>/<file>
#   themes/     color themes
#   icons/      fallback toolbar icons for the Firefox chrome (Adwaita's)
#   test/       make check

VERSION  = 0.1
CC      ?= cc
PREFIX  ?= /usr/local
CPPFLAGS += -Iinclude -I. -D_DEFAULT_SOURCE -DVERSION='"$(VERSION)"' \
	-DSYS_THEMES_DIR='"$(PREFIX)/share/themer/themes"' \
	-DSYS_TEMPLATES_DIR='"$(PREFIX)/share/themer/templates"' \
	-DSYS_MODULES_DIR='"$(PREFIX)/lib/themer/modules"' \
	-DSYS_ICONS_DIR='"$(PREFIX)/share/themer/icons"' \
	-DSYS_QT_PLUGINS_DIR='"$(PREFIX)/lib/themer/qt"'
CFLAGS  += -std=c99 -Wall -Wextra -pedantic -Os
LDLIBS   = -ldl -lm

SRC  = $(wildcard src/*.c)
OBJ  = $(SRC:.c=.o)
HOST = $(filter-out src/main.o src/tui.o,$(OBJ))   # the host without a front end
HDR  = src/themer.h include/themer_module.h config.h
MODS = $(patsubst modules/%.c,modules/%.so,$(wildcard modules/*.c))
MODH = $(wildcard modules/*.h) include/themer_module.h

# The graphical front ends are built only where their toolkit's development files
# are installed, found through pkg-config: themer-gtk needs gtk+-3.0 and a C
# compiler, themer-qt needs Qt6Widgets or Qt5Widgets and a C++ compiler. Without
# pkg-config nothing is probed and only themer is built. `make gui` says what was
# found. GTK=0 or QT=0 on the command line skips one on purpose.
PKGCONF ?= $(shell command -v pkg-config 2>/dev/null || command -v pkgconf 2>/dev/null)
CXX     ?= c++
CXXFLAGS += -std=c++17 -Wall -Wextra -Os -fPIC
HAVE_CXX := $(shell command -v $(CXX) >/dev/null 2>&1 && echo yes)
ifneq ($(PKGCONF),)
GTKPKG := $(shell $(PKGCONF) --exists gtk+-3.0 2>/dev/null && echo gtk+-3.0)
QTPKG  := $(shell $(PKGCONF) --exists Qt6Widgets 2>/dev/null && echo Qt6Widgets || \
            ($(PKGCONF) --exists Qt5Widgets 2>/dev/null && echo Qt5Widgets))
endif
ifeq ($(GTK),0)
GTKPKG :=
endif
ifeq ($(QT),0)
QTPKG :=
endif
GTKBIN := $(if $(GTKPKG),themer-gtk)
QTBIN  := $(if $(and $(QTPKG),$(HAVE_CXX)),themer-qt)
GUIS    = $(GTKBIN) $(QTBIN) themer-web   # themer-web needs no toolkit: always built

# themer's Qt platform theme plugin (gui/qpa), what Qt programs take the theme
# through without qt5ct/qt6ct: one build per Qt major found. It needs the QPA
# headers, which live in Qt's private include directory.
ifneq ($(PKGCONF),)
QT5 := $(shell $(PKGCONF) --exists Qt5Widgets 2>/dev/null && echo yes)
QT6 := $(shell $(PKGCONF) --exists Qt6Widgets 2>/dev/null && echo yes)
endif
ifeq ($(QT),0)
QT5 :=
QT6 :=
endif
QT5PRIV := $(shell $(PKGCONF) --variable=includedir Qt5Gui 2>/dev/null)/QtGui/$(shell $(PKGCONF) --modversion Qt5Gui 2>/dev/null)/QtGui
QT6PRIV := $(shell $(PKGCONF) --variable=includedir Qt6Gui 2>/dev/null)/QtGui/$(shell $(PKGCONF) --modversion Qt6Gui 2>/dev/null)/QtGui
QPA5 := $(if $(and $(QT5),$(HAVE_CXX)),gui/qpa/qt5/platformthemes/libthemer.so)
QPA6 := $(if $(and $(QT6),$(HAVE_CXX)),gui/qpa/qt6/platformthemes/libthemer.so)
QPAS  = $(QPA5) $(QPA6)

all: themer $(MODS) $(GUIS) $(QPAS) gui

config.h: config.def.h
	cp config.def.h config.h

# -rdynamic lets modules call the tm_* helpers in the host; every front end links
# the whole host, so the modules find them in each binary
themer: $(OBJ)
	$(CC) $(LDFLAGS) -rdynamic -o $@ $(OBJ) $(LDLIBS)

themer-gtk: gui/themer-gtk.o gui/common.o $(HOST)
	$(CC) $(LDFLAGS) -rdynamic -o $@ gui/themer-gtk.o gui/common.o $(HOST) $(LDLIBS) $(shell $(PKGCONF) --libs gtk+-3.0)

# the web front end: the page is built into the binary as a C string
gui/web_page.h: gui/web/index.html
	{ printf 'static const unsigned char web_page_bytes[] = {\n'; \
	  od -An -tx1 -v $< | sed 's/ *\([0-9a-f][0-9a-f]\)/0x\1,/g'; \
	  printf '0 };\nstatic const char *const web_page = (const char *)web_page_bytes;\n'; } > $@
gui/themer-web.o: gui/themer-web.c gui/web_page.h gui/common.h $(HDR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Igui -Isrc -c -o $@ gui/themer-web.c
themer-web: gui/themer-web.o gui/common.o $(HOST)
	$(CC) $(LDFLAGS) -rdynamic -o $@ gui/themer-web.o gui/common.o $(HOST) $(LDLIBS)

themer-qt: gui/themer-qt.o gui/common.o $(HOST)
	$(CXX) $(LDFLAGS) -rdynamic -o $@ gui/themer-qt.o gui/common.o $(HOST) $(LDLIBS) $(shell $(PKGCONF) --libs $(QTPKG))

gui/qpa/qt5/platformthemes/libthemer.so: gui/qpa/themerqpa.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -shared -I$(QT5PRIV) $(shell $(PKGCONF) --cflags Qt5Widgets) -o $@ $< $(shell $(PKGCONF) --libs Qt5Widgets)

gui/qpa/qt6/platformthemes/libthemer.so: gui/qpa/themerqpa.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -shared -I$(QT6PRIV) $(shell $(PKGCONF) --cflags Qt6Widgets) -o $@ $< $(shell $(PKGCONF) --libs Qt6Widgets)

# what the toolkit probe found
gui:
	@echo "pkg-config: $(if $(PKGCONF),$(PKGCONF),not found: no graphical front end is built)"
	@echo "themer-gtk: $(if $(GTKBIN),built (gtk+-3.0),skipped: gtk+-3.0 development files not found)"
	@echo "themer-qt:  $(if $(QTBIN),built ($(QTPKG)),skipped: $(if $(QTPKG),no C++ compiler ($(CXX)),Qt6Widgets or Qt5Widgets development files not found))"
	@echo "themer-web: built (serves on localhost while a browser module is on)"
	@echo "Qt platform theme plugin: $(if $(QPA5),Qt 5,)$(if $(and $(QPA5),$(QPA6)), and ,)$(if $(QPA6),Qt 6,)$(if $(QPAS),, none: Qt development files not found; Qt programs then need qt5ct/qt6ct)"

$(OBJ) gui/common.o gui/themer-gtk.o gui/themer-qt.o: $(HDR) gui/common.h

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

gui/common.o: gui/common.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -c -o $@ $<

gui/themer-gtk.o: gui/themer-gtk.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $(shell $(PKGCONF) --cflags gtk+-3.0) -c -o $@ $<

gui/themer-qt.o: gui/themer-qt.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Isrc $(shell $(PKGCONF) --cflags $(QTPKG)) -c -o $@ $<

modules/%.so: modules/%.c $(MODH)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared -o $@ $<

check: all
	sh test/run.sh

# Everything an earlier install put under PREFIX is removed first, so what was
# renamed or dropped upstream (a theme, a template set, a module, a front end that
# no longer builds here) does not linger. User files under ~/.config are never touched.
install: all uninstall
	install -Dm755 themer $(DESTDIR)$(PREFIX)/bin/themer
	for g in $(GUIS); do install -Dm755 $$g $(DESTDIR)$(PREFIX)/bin/$$g; done
	install -d $(DESTDIR)$(PREFIX)/share/themer/themes
	install -m644 themes/*.theme $(DESTDIR)$(PREFIX)/share/themer/themes/
	for d in templates/*/; do \
		for s in "$$d"*/; do \
			dst="$(DESTDIR)$(PREFIX)/share/themer/templates/$$(basename "$$d")/$$(basename "$$s")"; \
			install -d "$$dst"; \
			install -m644 "$$s"* "$$dst/"; \
		done; \
	done
	install -d $(DESTDIR)$(PREFIX)/share/themer/icons
	install -m644 icons/*.svg icons/README $(DESTDIR)$(PREFIX)/share/themer/icons/
	install -d $(DESTDIR)$(PREFIX)/lib/themer/modules
	install -m755 $(MODS) $(DESTDIR)$(PREFIX)/lib/themer/modules/
	install -Dm644 include/themer_module.h $(DESTDIR)$(PREFIX)/include/themer/themer_module.h
	for q in $(QPAS); do \
		v=$$(basename $$(dirname $$(dirname $$q))); \
		install -Dm755 $$q $(DESTDIR)$(PREFIX)/lib/themer/qt/$$v/platformthemes/libthemer.so; \
	done

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/themer $(DESTDIR)$(PREFIX)/bin/themer-gtk $(DESTDIR)$(PREFIX)/bin/themer-qt $(DESTDIR)$(PREFIX)/bin/themer-web
	rm -rf $(DESTDIR)$(PREFIX)/share/themer $(DESTDIR)$(PREFIX)/lib/themer $(DESTDIR)$(PREFIX)/include/themer

dist: clean
	git archive --prefix=themer-$(VERSION)/ -o themer-$(VERSION).tar.gz HEAD

clean:
	rm -f themer themer-gtk themer-qt themer-web $(OBJ) $(MODS) gui/*.o gui/web_page.h
	rm -rf gui/qpa/qt5 gui/qpa/qt6

.PHONY: all gui check install uninstall dist clean
