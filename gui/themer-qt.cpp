/* themer-qt.cpp: a Qt front end for themer (Qt 5 or 6), the twin of themer-gtk.
 *
 * A list of pages on the left (Theme, Fonts, Icons, Cursor, Modules), the preview
 * on the right, Apply at the bottom. The preview is ordinary Qt widgets, which take
 * the theme live: the application restyles itself from the qt.qss and the palette
 * the qt module would write for the theme being browsed. The Icons and Cursor
 * pages show the selected icon theme and cursor theme themselves. Nothing is
 * written until Apply, which runs the same apply as "themer apply" across every
 * module and saves the state.
 *
 * No moc: signals are connected to lambdas, and the custom widgets override
 * paintEvent only. The Makefile builds it when pkg-config finds Qt6Widgets or
 * Qt5Widgets. */
#include <QApplication>
#include <QBuffer>
#include <QButtonGroup>
#include <QColorDialog>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFontComboBox>
#include <QFileDialog>
#include <QFontDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QPlainTextEdit>
#include <QDialogButtonBox>
#include <QDir>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QToolButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <cstdio>
#include <functional>
#include <cstdlib>
#include <cstring>
#include "common.h"

#define MAX_LIST 256
#define ICON_PX 32
#define CURSOR_PX 32
#define MAX_CURSOR_PIX (512 * 512)

static Config cfg, applied;
static bool dirty, updating;
static QLabel *status, *title;
static QWidget *swatches, *sample, *cursorArea;
static QPushButton *applyButton;
static QListWidget *themeList;
static char theme_names[MAX_THEMES][NAME_LEN];
static Theme themes[MAX_THEMES];
static int ntheme_names;

static QColor hex(const char *h)
{
	return QColor(QString("#") + h);
}

static QString utf8(const char *s)
{
	return QString::fromUtf8(s);
}

/* a theme's swatch: bg, surf, acc, and a stroke of fg, for the list */
/* a template set as people read it (Themer Default); the directory name is the id */
static QString setTitle(const char *set)
{
	char t[NAME_LEN];
	return utf8(front_set_title(set, t, sizeof t));
}

/* a combo of the template sets, by title, the id as item data; current selected */
static void fillSets(QComboBox *c, const char *current)
{
	char names[MAX_LIST][NAME_LEN];
	int n = template_all_sets(names, MAX_LIST);

	c->clear();
	for (int i = 0; i < n; i++)
		c->addItem(setTitle(names[i]), utf8(names[i]));
	c->setCurrentIndex(c->findData(utf8(current)));
}

static QIcon themeSwatch(const Theme &t, int mode)
{
	QPixmap pm(40, 18);
	QPainter pr(&pm);

	pr.fillRect(0, 0, 16, 18, hex(t.hex[mode][C_BG]));
	pr.fillRect(16, 0, 12, 18, hex(t.hex[mode][C_SURF]));
	pr.fillRect(28, 0, 12, 18, hex(t.hex[mode][C_ACC]));
	pr.fillRect(4, 8, 8, 2, hex(t.hex[mode][C_FG]));
	return QIcon(pm);
}

/* ---- the live preview: the application takes the rendered Qt stylesheet and a
 * palette from the theme ---- */

static void restyle()
{
	static char qss[256 * 1024];
	Scheme s;

	gui_scheme(&cfg, &s);
	QPalette p;
	p.setColor(QPalette::Window, hex(s.hex[C_BG]));
	p.setColor(QPalette::WindowText, hex(s.hex[C_FG]));
	p.setColor(QPalette::Base, hex(s.hex[C_BG]));
	p.setColor(QPalette::AlternateBase, hex(s.hex[C_SURF]));
	p.setColor(QPalette::Text, hex(s.hex[C_FG]));
	p.setColor(QPalette::Button, hex(s.hex[C_SURF]));
	p.setColor(QPalette::ButtonText, hex(s.hex[C_FG]));
	p.setColor(QPalette::Highlight, hex(s.hex[C_ACC]));
	p.setColor(QPalette::HighlightedText, hex(s.hex[C_ACCFG]));
	p.setColor(QPalette::Light, hex(s.hex[C_SURF]));
	p.setColor(QPalette::Dark, hex(s.line));
	p.setColor(QPalette::Mid, hex(s.line));
	p.setColor(QPalette::Shadow, hex(s.line));
	p.setColor(QPalette::Link, hex(s.hex[C_ACC]));
	p.setColor(QPalette::ToolTipBase, hex(s.hex[C_BG]));
	p.setColor(QPalette::ToolTipText, hex(s.hex[C_FG]));
	p.setColor(QPalette::PlaceholderText, hex(s.ansi[8]));
	qApp->setPalette(p);
	QFont f(utf8(cfg.ui_font), cfg.ui_size);
	/* the font rendering, as fontconfig and the Qt settings would carry it after Apply */
	f.setStyleStrategy(cfg.antialias ? QFont::PreferAntialias : QFont::NoAntialias);
	f.setHintingPreference(!strcmp(cfg.hinting, "none") ? QFont::PreferNoHinting
	                       : !strcmp(cfg.hinting, "full") ? QFont::PreferFullHinting
	                       : QFont::PreferVerticalHinting);
	qApp->setFont(f);
	/* what the qt module renders: from its own set (its template option) */
	static Config view;
	view = cfg;
	snprintf(view.set, sizeof view.set, "%s", front_template_set(&cfg, "qt"));
	if (front_render(&view, "qt", "qt", "qt.qss", qss, sizeof qss) == 0) {
		/* the window's own rule: the chosen one of a row of buttons in the accent */
		Scheme sc;
		gui_scheme(&cfg, &sc);
		qApp->setStyleSheet(utf8(qss) + QString("\nQPushButton[seg=\"true\"]:checked { background: #%1; color: #%2; }\n")
		                    .arg(sc.hex[C_ACC]).arg(sc.hex[C_ACCFG]));
	}
	if (title)
		title->setText(utf8(cfg.th.name) + "  \xc2\xb7  " + (cfg.mode == MODE_DARK ? "dark" : "light") +
		               "  \xc2\xb7  " + setTitle(cfg.set));
	if (themeList) {
		int m = cfg.mode == MODE_DARK ? 0 : 1;
		for (int i = 0; i < ntheme_names; i++)
			themeList->item(i)->setIcon(themeSwatch(themes[i], m));
	}
	if (swatches)
		swatches->update();
	if (sample)
		sample->update();
	if (cursorArea)
		cursorArea->update();
}

static void markDirty()
{
	dirty = true;
	status->setText("unapplied changes");
	applyButton->setEnabled(true);
}

static std::function<void()> afterChange;   /* the window's own follow-ups (the colors shown) */

static void changed()
{
	if (updating)
		return;
	markDirty();
	restyle();
	if (afterChange)
		afterChange();
}

/* ---- the swatches, the terminal transcript, and the cursors ---- */

class Swatches : public QWidget {
public:
	using QWidget::QWidget;
	QSize sizeHint() const override { return QSize(400, 80); }
protected:
	void paintEvent(QPaintEvent *) override
	{
		static const char *role_names[C_N] = { "bg", "fg", "surf", "acc", "accfg", "urg", "warn" };
		Scheme s;
		QPainter pr(this);
		int cell = qMax(8, width() / 16);

		gui_scheme(&cfg, &s);
		pr.fillRect(rect(), hex(s.hex[C_BG]));
		pr.setFont(QFont(utf8(cfg.mono_font), 8));
		for (int i = 0; i < C_N; i++) {
			pr.fillRect(i * cell * 2, 0, cell * 2 - 2, 28, hex(s.hex[i]));
			/* the label in whichever of fg and bg reads better on the swatch */
			pr.setPen(hex(tm_contrast(s.hex[i], s.hex[C_FG]) >= tm_contrast(s.hex[i], s.hex[C_BG])
			              ? s.hex[C_FG] : s.hex[C_BG]));
			pr.drawText(i * cell * 2 + 4, 18, role_names[i]);
		}
		for (int i = 0; i < 16; i++)
			pr.fillRect((i % 8) * cell * 2, 34 + (i / 8) * 22, cell * 2 - 2, 20, hex(s.ansi[i]));
	}
};

class Sample : public QWidget {
public:
	using QWidget::QWidget;
	QSize sizeHint() const override { return QSize(400, 200); }
protected:
	void paintEvent(QPaintEvent *) override
	{
		Scheme s;
		QPainter pr(this);
		QFont f(utf8(cfg.mono_font), cfg.mono_size);
		const char *colors[6];
		int y = 8;

		gui_scheme(&cfg, &s);
		colors[0] = s.ansi[2]; colors[1] = s.ansi[4]; colors[2] = s.ansi[1];
		colors[3] = s.ansi[3]; colors[4] = s.hex[C_FG]; colors[5] = s.ansi[5];
		pr.fillRect(rect(), hex(s.hex[C_BG]));
		pr.setPen(hex(s.line));
		pr.drawRect(rect().adjusted(0, 0, -1, -1));
		pr.setFont(f);
		QFontMetrics fm(f);
		for (int i = 0; i < gui_nsample_lines; i++) {
			pr.setPen(hex(colors[i % 6]));
			pr.drawText(8, y + fm.ascent(), utf8(gui_sample_lines[i]));
			y += fm.height() + 2;
		}
		pr.fillRect(8, y, width() - 16, fm.height() + 4, hex(s.hex[C_ACC]));
		pr.setPen(hex(s.hex[C_ACCFG]));
		pr.drawText(8, y + 2 + fm.ascent(), " selected text, in the accent ");
	}
};

/* the cursor theme's own files, drawn at the chosen size */
class Cursors : public QWidget {
public:
	using QWidget::QWidget;
	QSize sizeHint() const override { return QSize(400, 130); }
	QSize minimumSizeHint() const override { return QSize(200, 130); }
protected:
	void paintEvent(QPaintEvent *) override
	{
		Scheme s;
		QPainter pr(this);
		QFontMetrics fm(font());
		int px = cfg.cursor_size, x = 12, y = 12, found = 0;

		gui_scheme(&cfg, &s);
		load();
		pr.fillRect(rect(), hex(s.hex[C_BG]));
		pr.setPen(hex(s.line));
		pr.drawRect(rect().adjusted(0, 0, -1, -1));
		for (int i = 0; i < n; i++) {
			int cellw = qMax(72, px + 16);

			if (x + cellw > width() - 12 && x > 12) {
				x = 12;
				y += px + 36;
			}
			if (img[i].ok) {
				QImage im((const uchar *)img[i].pix, img[i].w, img[i].h, img[i].w * 4,
				          QImage::Format_ARGB32_Premultiplied);
				QImage scaled = im.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
				pr.drawImage(x + (cellw - scaled.width()) / 2, y, scaled);
				found++;
			}
			pr.setPen(hex(s.hex[C_FG]));
			QString label = utf8(gui_preview_cursors[i][1]);
			pr.drawText(x + (cellw - fm.horizontalAdvance(label)) / 2, y + px + 6 + fm.ascent(), label);
			x += cellw + 8;
		}
		if (!found) {
			pr.setPen(hex(s.hex[C_FG]));
			pr.drawText(12, 12 + fm.ascent(), "no cursor theme of that name is installed");
		}
		setMinimumHeight(y + px + 40);
	}
private:
	struct { unsigned *pix; int w, h, ok; } img[16] = {};
	int n = 0;
	char loadedFor[NAME_LEN] = "";

	void load()
	{
		if (!strcmp(loadedFor, cfg.cursor) && n)
			return;
		snprintf(loadedFor, sizeof loadedFor, "%s", cfg.cursor);
		int i;
		for (i = 0; gui_preview_cursors[i][0] && i < 16; i++) {
			if (!img[i].pix)
				img[i].pix = new unsigned[MAX_CURSOR_PIX];
			img[i].ok = gui_cursor_image(cfg.cursor, gui_preview_cursors[i][0], CURSOR_PX,
			                             img[i].pix, MAX_CURSOR_PIX, &img[i].w, &img[i].h) == 0;
		}
		n = i;
	}
};

/* ---- the window ---- */

class Window : public QMainWindow {
public:
	Window()
	{
		setWindowTitle("themer");
		setWindowIcon(QIcon::fromTheme("preferences-desktop-theme"));
		resize(1200, 700);
		menus();

		QWidget *central = new QWidget;
		QVBoxLayout *v = new QVBoxLayout(central);
		v->setContentsMargins(0, 0, 0, 0);
		QSplitter *split = new QSplitter;
		split->addWidget(settings());
		split->addWidget(preview());
		split->setStretchFactor(0, 2);
		split->setStretchFactor(1, 1);
		split->setSizes(QList<int>() << 780 << 420);   /* the settings get the room they need */
		v->addWidget(split, 1);

		/* the action bar: status on the left, Apply on the right */
		QHBoxLayout *actions = new QHBoxLayout;
		actions->setContentsMargins(12, 6, 12, 8);
		status = new QLabel("up to date");
		actions->addWidget(status, 1);
		applyButton = new QPushButton("Apply");
		applyButton->setDefault(true);
		applyButton->setEnabled(false);
		QObject::connect(applyButton, &QPushButton::clicked, []() { doApply(); });
		actions->addWidget(applyButton);
		v->addLayout(actions);
		setCentralWidget(central);
	}

	/* themer-qt [theme|fonts|icons|cursor|modules] opens on that page */
	/* another front end saved: its changes are merged in (front_sync) and shown */
	void syncTick()
	{
		if (!front_sync())
			return;
		showCfg();
		dirty = !config_same(&cfg, &applied);
		applyButton->setEnabled(dirty);
		status->setText("updated with changes made in another front end");
	}

	void openPage(const char *name, const char *module = nullptr)
	{
		static const char *pages[] = { "theme", "fonts", "icons", "cursor", "modules", "env", "server" };
		for (int i = 0; i < 6; i++)
			if (!strcmp(pages[i], name))
				pageList->setCurrentRow(i);
		/* themer-qt modules MODULE opens on that module */
		if (module && !strcmp(name, "modules"))
			for (int k = 0; k < nmodules; k++)
				if (!strcmp(themer_module_name(moduleOrder[k]), module))
					mods->setCurrentItem(mods->topLevelItem(k));
	}
protected:
	void closeEvent(QCloseEvent *e) override
	{
		if (!dirty) {
			e->accept();
			return;
		}
		QMessageBox box(QMessageBox::Question, "themer", "There are unapplied changes.",
		                QMessageBox::NoButton, this);
		QPushButton *discard = box.addButton("Discard", QMessageBox::DestructiveRole);
		QPushButton *apply = box.addButton("Apply", QMessageBox::AcceptRole);
		box.addButton("Keep editing", QMessageBox::RejectRole);
		box.exec();
		if (box.clickedButton() == apply)
			doApply();
		if (box.clickedButton() == apply || box.clickedButton() == discard)
			e->accept();
		else
			e->ignore();
	}
private:
	QListWidget *pageList = nullptr;
	QStackedWidget *pages = nullptr;
	QPushButton *dark = nullptr, *light = nullptr;
	QComboBox *sets = nullptr, *icons = nullptr, *cursorBox = nullptr, *hinting = nullptr, *rgba = nullptr;
	QPushButton *uiFont = nullptr, *monoFont = nullptr;
	QSpinBox *cursorSize = nullptr, *uiSize = nullptr, *monoSize = nullptr;
	QCheckBox *antialias = nullptr;
	QComboBox *ttyBox = nullptr;
	QGridLayout *colorsGrid = nullptr;
	QVBoxLayout *envBox = nullptr;
	QLineEdit *envName = nullptr, *envValue = nullptr;
	QTreeWidget *mods = nullptr;
	QGridLayout *iconGrid = nullptr;
	int moduleOrder[MAX_MODULES];
	int nmodules = 0;

	static void doApply()
	{
		status->setText("applying...");
		qApp->processEvents();
		int fails = front_apply(&cfg);
		applied = cfg;
		dirty = false;
		applyButton->setEnabled(false);
		status->setText(fails ? "applied with failures; see the terminal"
		                      : "applied. Qt apps keep their theme until restarted; restart Firefox and Thunderbird");
	}

	void menus()
	{
		QMenu *file = menuBar()->addMenu("&File");
		QAction *apply = file->addAction("&Apply", []() { doApply(); });
		apply->setShortcut(QKeySequence("Ctrl+S"));
		file->addAction("&Revert to applied", [this]() { revert(); });
		file->addSeparator();
		QAction *quit = file->addAction("&Quit", [this]() { close(); });
		quit->setShortcut(QKeySequence("Ctrl+Q"));
		QMenu *help = menuBar()->addMenu("&Help");
		help->addAction("&About themer", [this]() {
			QMessageBox::about(this, "themer",
			                   "One theme for the whole desktop: GTK, Qt, Firefox, Thunderbird, "
			                   "the window manager, the terminals, the TTY.\n\nThe preview is the "
			                   "real thing: this window is styled by the stylesheet Apply writes.");
		});
	}

	/* back to what was last applied */
	void revert()
	{
		cfg = applied;
		showCfg();
		dirty = false;
		applyButton->setEnabled(false);
		status->setText("back to the applied settings");
	}

	/* every control set from cfg */
	void showCfg()
	{
		updating = true;
		for (int i = 0; i < ntheme_names; i++)
			if (!strcmp(theme_names[i], cfg.th.name))
				themeList->setCurrentRow(i);
		(cfg.mode == MODE_DARK ? dark : light)->setChecked(true);
		fillSets(sets, cfg.set);   /* refilled: a set may have been made elsewhere */
		fontLabel(uiFont, cfg.ui_font, cfg.ui_size);
		fontLabel(monoFont, cfg.mono_font, cfg.mono_size);
		uiSize->setValue(cfg.ui_size);
		monoSize->setValue(cfg.mono_size);
		icons->setCurrentText(utf8(cfg.icons));
		cursorBox->setCurrentText(utf8(cfg.cursor));
		cursorSize->setValue(cfg.cursor_size);
		hinting->setCurrentText(utf8(cfg.hinting));
		antialias->setChecked(cfg.antialias);
		rgba->setCurrentText(utf8(cfg.rgba));
		ttyBox->setCurrentText(utf8(cfg.tty_font));
		colorsFill();
		envFill();
		refreshStates();
		showModule(modSel);
		updating = false;
		fillIcons();
		restyle();
	}

	static QLabel *heading(const char *text)
	{
		QLabel *l = new QLabel(QString("<b>") + text + "</b>");
		l->setContentsMargins(0, 6, 0, 2);
		return l;
	}

	static QLabel *caption(const char *text)
	{
		QLabel *l = new QLabel(text);
		l->setWordWrap(true);
		l->setEnabled(false);      /* drawn in the disabled text color, as a hint */
		return l;
	}

	static QWidget *page(QVBoxLayout *&v)
	{
		QWidget *w = new QWidget;
		v = new QVBoxLayout(w);
		v->setContentsMargins(12, 8, 12, 12);
		v->setSpacing(6);
		return w;
	}

	static QComboBox *nameCombo(char names[][NAME_LEN], int n, const char *current)
	{
		QComboBox *c = new QComboBox;
		c->setEditable(true);
		for (int i = 0; i < n; i++)
			c->addItem(utf8(names[i]));
		c->setCurrentText(utf8(current));
		return c;
	}

	static void fontLabel(QPushButton *b, const char *fam, int size)
	{
		(void)size;          /* the size has its own field */
		b->setText(utf8(fam));
	}

	/* a button of a row of choices: checkable, drawn in the accent when chosen */
	static QPushButton *segButton(const QString &text)
	{
		QPushButton *b = new QPushButton(text);
		b->setCheckable(true);
		b->setProperty("seg", true);
		return b;
	}

	/* a button that opens the font dialog and writes the family and size */
	QPushButton *fontButton(char *fam, size_t n, int *size, bool mono)
	{
		QPushButton *b = new QPushButton;
		fontLabel(b, fam, *size);
		QObject::connect(b, &QPushButton::clicked, [this, b, fam, n, size, mono]() {
			bool ok;
			QFontDialog::FontDialogOptions opt = mono ? QFontDialog::MonospacedFonts : QFontDialog::FontDialogOptions();
			QFont f = QFontDialog::getFont(&ok, QFont(utf8(fam), *size), this,
			                               mono ? "Terminal font" : "Interface font", opt);
			if (!ok)
				return;
			/* the family only: the size is the field beside the button */
			if (front_set_font(&cfg, mono, f.family().toUtf8().constData(), 0) < 0)
				return;
			(void)n;
			fontLabel(b, fam, *size);
			changed();
		});
		return b;
	}

	QWidget *settings()
	{
		QWidget *w = new QWidget;
		QHBoxLayout *h = new QHBoxLayout(w);
		h->setContentsMargins(0, 0, 0, 0);
		h->setSpacing(0);
		pageList = new QListWidget;
		pageList->addItems(QStringList() << "Theme" << "Fonts" << "Icons" << "Cursor" << "Modules" << "Environment" << "Server");
		pageList->setFixedWidth(130);
		pageList->setFrameShape(QFrame::NoFrame);
		pages = new QStackedWidget;
		pages->addWidget(pageTheme());
		pages->addWidget(pageFonts());
		pages->addWidget(pageIcons());
		pages->addWidget(pageCursor());
		pages->addWidget(pageModules());
		pages->addWidget(pageEnv());
		pages->addWidget(pageServer());
		QObject::connect(pageList, &QListWidget::currentRowChanged, [this](int r) { pages->setCurrentIndex(r); });
		pageList->setCurrentRow(0);
		QScrollArea *scroll = new QScrollArea;
		scroll->setWidget(pages);
		scroll->setWidgetResizable(true);
		scroll->setFrameShape(QFrame::NoFrame);
		scroll->setMinimumWidth(380);
		h->addWidget(pageList);
		QFrame *line = new QFrame;
		line->setFrameShape(QFrame::VLine);
		h->addWidget(line);
		h->addWidget(scroll, 1);
		return w;
	}

	/* ---- template sets: new, import, check ---- */

	void refillSets()
	{
		updating = true;
		fillSets(sets, cfg.set);
		updating = false;
	}

	/* runs fn with its report in memory, shows the report, returns fn's result */
	template <class F> int withReport(const char *title, F fn)
	{
		char *buf = nullptr;
		size_t len = 0;
		FILE *f = open_memstream(&buf, &len);
		if (!f)
			return -1;
		int r = fn(f);
		fclose(f);
		QDialog d(this);
		d.setWindowTitle(title);
		QVBoxLayout *v = new QVBoxLayout(&d);
		QPlainTextEdit *t = new QPlainTextEdit;
		t->setReadOnly(true);
		t->setFont(QFont(utf8(cfg.mono_font), cfg.mono_size));
		t->setPlainText(buf && len ? utf8(buf) : "(nothing to report)");
		v->addWidget(t);
		QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Close);
		QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
		v->addWidget(bb);
		d.resize(680, 400);
		d.exec();
		free(buf);
		return r;
	}

	void setNew()
	{
		bool ok;
		QString name = QInputDialog::getText(this, "New template set",
		                                     "A copy of the current set under this name, in ~/.config/themer/templates/, "
		                                     "to edit. Letters, digits, - and _.", QLineEdit::Normal, "", &ok);
		if (!ok || name.isEmpty())
			return;
		QByteArray n = name.toUtf8();
		if (withReport("New set", [&](FILE *f) { return front_set_copy(cfg.set, n.constData(), f) < 0 ? -1 : 0; }) < 0) {
			status->setText("could not copy the set; the name may be taken or not valid");
			return;
		}
		if (front_set_set(&cfg, n.constData()) == 0) {
			refillSets();
			changed();
		}
	}

	void setImport()
	{
		QDialog d(this);
		d.setWindowTitle("Import a theme into a set");
		QVBoxLayout *v = new QVBoxLayout(&d);
		v->addWidget(caption("The theme's files go under the module's names in the set, with every color "
		                     "that matches the current theme turned into a variable, so it follows the "
		                     "color theme. Import with the theme and mode the look was made for."));
		QFormLayout *form = new QFormLayout;
		QLineEdit *name = new QLineEdit(strcmp(cfg.set, TM_DEFAULT_SET) ? utf8(cfg.set) : "");
		name->setPlaceholderText("new or existing set name");
		QComboBox *module = new QComboBox;
		for (int i = 0; i < themer_module_count(); i++)
			if (themer_module_templates(i))
				module->addItem(utf8(themer_module_name(i)));
		QHBoxLayout *ph = new QHBoxLayout;
		QLineEdit *path = new QLineEdit;
		path->setPlaceholderText("a theme directory or file");
		QPushButton *pf = new QPushButton("File\u2026"), *pd = new QPushButton("Folder\u2026");
		QObject::connect(pf, &QPushButton::clicked, [&]() {
			QString s = QFileDialog::getOpenFileName(&d, "The theme file");
			if (!s.isEmpty()) path->setText(s);
		});
		QObject::connect(pd, &QPushButton::clicked, [&]() {
			QString s = QFileDialog::getExistingDirectory(&d, "The theme directory");
			if (!s.isEmpty()) path->setText(s);
		});
		ph->addWidget(path, 1);
		ph->addWidget(pf);
		ph->addWidget(pd);
		form->addRow("Set", name);
		form->addRow("Module", module);
		form->addRow("Theme", ph);
		v->addLayout(form);
		QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		bb->button(QDialogButtonBox::Ok)->setText("Import");
		QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
		QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
		v->addWidget(bb);
		if (d.exec() != QDialog::Accepted || name->text().isEmpty() || path->text().isEmpty())
			return;
		QByteArray n = name->text().toUtf8(), m = module->currentText().toUtf8(), p = path->text().toUtf8();
		if (withReport("Import", [&](FILE *f) { return set_import(&cfg, n.constData(), m.constData(), p.constData(), f); }) < 0) {
			status->setText("import failed; see the report");
			return;
		}
		if (front_set_set(&cfg, n.constData()) == 0) {
			refillSets();
			changed();
		}
	}

	/* ---- the modules page: the list on the left, the selected module on the right,
	 * its options in the sections every front end uses (front_option_group). A switch
	 * loads or unloads at once and an option takes effect at once, as in the TUI,
	 * themer-gtk and themer-web; Apply is for the theme, fonts, icons and cursor. ---- */

	QScrollArea *detail = nullptr;
	int modSel = -1;

	static QString stateText(int i)
	{
		return !themer_module_present(&cfg, i) ? "not installed" : themer_module_on(&cfg, i) ? "on" : "off";
	}

	void refreshStates()
	{
		for (int k = 0; k < nmodules; k++)
			mods->topLevelItem(k)->setText(1, stateText(moduleOrder[k]));
	}

	/* the detail is rebuilt after the signal that changed it has returned */
	void rebuildLater()
	{
		QTimer::singleShot(0, [this]() { showModule(modSel); });
	}

	void setOpt(int i, int j, const QString &value)
	{
		char msg[256];
		QByteArray b = value.toUtf8();

		if (updating || b == themer_option_value(&cfg, i, j))
			return;
		front_option_set(&cfg, i, j, b.constData(), msg, sizeof msg);
		status->setText(utf8(msg));
		restyle();
		rebuildLater();
	}

	/* a control for one option: a check box for on|off, buttons for mode, a combo for
	 * other choices and for the template set, a spin box for a range, a line edit
	 * otherwise */
	QWidget *optionControl(int i, int j)
	{
		const TmOption *o = themer_option(i, j);
		const char *cur = themer_option_value(&cfg, i, j);
		QWidget *w;

		switch (front_option_control(i, j)) {
		case OC_SWITCH: {
			QCheckBox *c = new QCheckBox(!strcmp(cur, "on") ? "on" : "off");
			c->setChecked(!strcmp(cur, "on"));
			QObject::connect(c, &QCheckBox::toggled, [this, i, j](bool on) { setOpt(i, j, on ? "on" : "off"); });
			w = c;
			break;
		}
		case OC_BUTTONS: {
			/* the choices side by side, the chosen one in the accent */
			QWidget *row = new QWidget;
			QHBoxLayout *h = new QHBoxLayout(row);
			QButtonGroup *group = new QButtonGroup(row);
			h->setContentsMargins(0, 0, 0, 0);
			h->setSpacing(0);
			for (const QString &v : utf8(o->choices).split('|')) {
				QPushButton *b = segButton(v);
				b->setChecked(v == utf8(cur));
				group->addButton(b);
				QObject::connect(b, &QPushButton::clicked, [this, i, j, v]() { setOpt(i, j, v); });
				h->addWidget(b);
			}
			h->addStretch();
			w = row;
			break;
		}
		case OC_LIST: {
			QComboBox *c = new QComboBox;
			if (!strcmp(o->name, "template")) {
				fillSets(c, cur);
				QObject::connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged),
				                 [this, i, j, c](int) { setOpt(i, j, c->currentData().toString()); });
			} else {
				for (const QString &v : utf8(o->choices).split('|'))
					c->addItem(v);
				c->setCurrentText(utf8(cur));
				QObject::connect(c, &QComboBox::currentTextChanged, [this, i, j](const QString &v) { setOpt(i, j, v); });
			}
			w = c;
			break;
		}
		case OC_SLIDER: {
			/* a bar to drag, its value beside it; set when it is let go */
			QWidget *row = new QWidget;
			QHBoxLayout *h = new QHBoxLayout(row);
			QSlider *sl = new QSlider(Qt::Horizontal);
			QLabel *val = new QLabel(utf8(cur));
			h->setContentsMargins(0, 0, 0, 0);
			sl->setRange(o->min, o->max);
			sl->setValue(atoi(cur));
			sl->setFixedWidth(200);
			val->setMinimumWidth(28);
			QObject::connect(sl, &QSlider::valueChanged, [this, i, j, sl, val](int v) {
				val->setText(QString::number(v));
				if (!sl->isSliderDown())
					setOpt(i, j, QString::number(v));   /* keys and clicks */
			});
			QObject::connect(sl, &QSlider::sliderReleased, [this, i, j, sl]() { setOpt(i, j, QString::number(sl->value())); });
			h->addWidget(sl);
			h->addWidget(val);
			h->addStretch();
			w = row;
			break;
		}
		default: {
			QLineEdit *e = new QLineEdit(utf8(cur));
			e->setPlaceholderText(utf8(o->def));
			QObject::connect(e, &QLineEdit::editingFinished, [this, i, j, e]() { setOpt(i, j, e->text()); });
			w = e;
		}
		}
		w->setToolTip(utf8(o->desc));
		return w;
	}

	void showModule(int i)
	{
		modSel = i;
		if (!detail || i < 0)
			return;
		updating = true;
		QWidget *box = new QWidget;
		QVBoxLayout *v = new QVBoxLayout(box);
		v->setContentsMargins(12, 10, 12, 10);

		QHBoxLayout *top = new QHBoxLayout;
		QLabel *name = new QLabel(QString("<b><big>%1</big></b>").arg(utf8(themer_module_name(i)).toHtmlEscaped()));
		QCheckBox *on = new QCheckBox("On");
		on->setChecked(themer_module_on(&cfg, i));
		on->setToolTip("on: Apply writes this program's settings; off: they are removed");
		QObject::connect(on, &QCheckBox::toggled, [this, i](bool want) {
			if (updating || want == (bool)themer_module_on(&cfg, i))
				return;
			if (front_module_set(&cfg, i, want) < 0)
				status->setText(want ? "the module did not load; see the terminal" : "unload had errors");
			else
				status->setText(want ? "module loaded" : "module unloaded");
			refreshStates();
			rebuildLater();
		});
		top->addWidget(name, 1);
		top->addWidget(on);
		v->addLayout(top);
		v->addWidget(caption(themer_module_desc(i)));
		if (!themer_module_present(&cfg, i))
			v->addWidget(caption(themer_module_on(&cfg, i)
			             ? "Its program is not installed here; the module stays on and takes effect once it appears."
			             : "Its program is not installed here."));

		int no = themer_option_count(i);
		for (int g = 0; g < OG_N; g++) {
			QGridLayout *grid = nullptr;
			int row = 0;
			for (int j = 0; j < no; j++) {
				const TmOption *o = themer_option(i, j);
				if (front_option_group(i, j) != g)
					continue;
				if (!grid) {
					v->addSpacing(6);
					v->addWidget(heading(front_option_groups[g]));
					QFrame *line = new QFrame;
					line->setFrameShape(QFrame::HLine);
					line->setFrameShadow(QFrame::Sunken);
					v->addWidget(line);
					grid = new QGridLayout;
					grid->setContentsMargins(6, 0, 0, 0);
					grid->setVerticalSpacing(2);
					grid->setColumnMinimumWidth(0, 100);
					grid->setColumnStretch(3, 1);
					v->addLayout(grid);
				}
				QLabel *l = new QLabel(utf8(o->name));
				l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
				grid->addWidget(l, row, 0);
				if (front_option_action(&cfg, i, j)) {
					/* the control, then its action (Install for a stylus site) */
					QHBoxLayout *hb = new QHBoxLayout;
					QPushButton *ab = new QPushButton(utf8(front_option_action(&cfg, i, j)));
					ab->setToolTip("opens the style in your browser, where Stylus offers to install it");
					QObject::connect(ab, &QPushButton::clicked, [this, i, j]() {
						char msg[256];
						front_option_run(&cfg, i, j, msg, sizeof msg);
						status->setText(utf8(msg));
					});
					hb->addWidget(optionControl(i, j));
					hb->addWidget(ab);
					hb->addStretch();
					grid->addLayout(hb, row, 1);
				} else {
					grid->addWidget(optionControl(i, j), row, 1);
				}
				QString note = utf8(o->desc);
				if (themer_option_is_set(&cfg, i, j)) {
					QPushButton *r = new QPushButton("Reset");
					QObject::connect(r, &QPushButton::clicked, [this, i, j]() {
						char msg[256];
						front_option_reset(&cfg, i, j, msg, sizeof msg);
						status->setText(utf8(msg));
						restyle();
						rebuildLater();
					});
					grid->addWidget(r, row, 2);
					note += QString(" · default: %1").arg(!strcmp(o->name, "template") ? QString("the global set") : utf8(o->def));
				}
				QLabel *d = caption(note.toUtf8().constData());
				d->setContentsMargins(0, 0, 0, 6);
				grid->addWidget(d, row + 1, 1, 1, 3);
				row += 2;
			}
		}
		if (!no)
			v->addWidget(caption("This module has no options."));
		v->addStretch();
		detail->setWidget(box);       /* deletes the previous one */
		updating = false;
	}

	/* the scheme list, refilled after an import or a removal */
	static void fillThemes()
	{
		updating = true;
		themeList->clear();
		ntheme_names = theme_list(theme_names, MAX_THEMES);
		for (int i = 0; i < ntheme_names; i++) {
			theme_read(theme_names[i], &themes[i]);
			themeList->addItem(new QListWidgetItem(themeSwatch(themes[i], cfg.mode == MODE_DARK ? 0 : 1),
			                                       utf8(theme_names[i])));
			if (!strcmp(theme_names[i], cfg.th.name))
				themeList->setCurrentRow(i);
		}
		updating = false;
	}

	void schemeImport()
	{
		QString path = QFileDialog::getOpenFileName(this, "Import a colour scheme");
		if (path.isEmpty())
			return;
		QByteArray p = path.toUtf8();
		int n = withReport("Import", [&](FILE *f) { return scheme_import(p.constData(), nullptr, f) > 0 ? 0 : -1; });
		status->setText(n == 0 ? "imported; it is in the list" : "nothing imported");
		fillThemes();
	}

	void schemeRemove()
	{
		QListWidgetItem *it = themeList->currentItem();
		if (!it)
			return;
		QByteArray name = it->text().toUtf8();
		if (!strcmp(name.constData(), applied.th.name)) {
			status->setText("that is the scheme in use: apply another first");
			return;
		}
		if (scheme_remove(name.constData()) < 0) {
			status->setText("could not remove it");
			return;
		}
		status->setText(it->text() + " removed");
		if (!strcmp(cfg.th.name, name.constData())) {
			front_set_theme(&cfg, applied.th.name);
			changed();
		}
		fillThemes();
	}

	QWidget *pageTheme()
	{
		QVBoxLayout *v;
		QWidget *w = page(v);

		v->addWidget(heading("Color scheme"));
		QLineEdit *find = new QLineEdit;
		find->setPlaceholderText("Find a scheme");
		find->setClearButtonEnabled(true);
		v->addWidget(find);
		themeList = new QListWidget;
		themeList->setIconSize(QSize(40, 18));
		fillThemes();
		QObject::connect(find, &QLineEdit::textChanged, [](const QString &q) {
			for (int i = 0; i < themeList->count(); i++)
				themeList->item(i)->setHidden(!q.isEmpty() && !themeList->item(i)->text().contains(q));
		});
		QObject::connect(themeList, &QListWidget::currentTextChanged, [](const QString &t) {
			if (updating || t.isEmpty())
				return;
			QByteArray b = t.toUtf8();
			if (!strcmp(b.constData(), cfg.th.name))
				return;
			if (front_set_theme(&cfg, b.constData()) < 0) {
				status->setText("cannot read that theme");
				return;
			}
			changed();
		});
		v->addWidget(themeList, 1);
		{
			QHBoxLayout *bar = new QHBoxLayout;
			QPushButton *imp = new QPushButton("Import…"), *rem = new QPushButton("Remove"), *res = new QPushButton("Restore hidden");
			imp->setToolTip("A colour scheme file: a .theme, Xresources, kitty, foot or alacritty colors, or Gogh lines");
			rem->setToolTip("Delete the selected scheme if it is yours, or hide it if it is built in");
			res->setToolTip("Bring back every built-in scheme that was removed");
			QObject::connect(imp, &QPushButton::clicked, [this]() { schemeImport(); });
			QObject::connect(rem, &QPushButton::clicked, [this]() { schemeRemove(); });
			QObject::connect(res, &QPushButton::clicked, [this]() {
				status->setText(scheme_restore(nullptr) == 0 ? "every hidden built-in scheme is back" : "could not restore");
				fillThemes();
			});
			bar->addWidget(imp);
			bar->addWidget(rem);
			bar->addWidget(res);
			bar->addStretch();
			v->addLayout(bar);
		}

		QGridLayout *g = new QGridLayout;
		/* dark and light side by side, the chosen one in the accent, as every
		 * module's mode option */
		QHBoxLayout *mode = new QHBoxLayout;
		QButtonGroup *mg = new QButtonGroup(w);
		mode->setSpacing(0);
		dark = segButton("dark");
		light = segButton("light");
		mg->addButton(dark);
		mg->addButton(light);
		(cfg.mode == MODE_DARK ? dark : light)->setChecked(true);
		QObject::connect(dark, &QPushButton::toggled, [](bool on) { if (on && !updating) { cfg.mode = MODE_DARK; changed(); } });
		QObject::connect(light, &QPushButton::toggled, [](bool on) { if (on && !updating) { cfg.mode = MODE_LIGHT; changed(); } });
		mode->addWidget(dark);
		mode->addWidget(light);
		mode->addStretch();
		g->addWidget(new QLabel("Mode"), 0, 0, Qt::AlignRight);
		g->addLayout(mode, 0, 1);
		sets = new QComboBox;
		fillSets(sets, cfg.set);
		QObject::connect(sets, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int) {
			QString id = sets->currentData().toString();
			if (updating || id.isEmpty())
				return;
			if (front_set_set(&cfg, id.toUtf8().constData()) == 0)
				changed();
		});
		g->addWidget(new QLabel("Template set"), 1, 0, Qt::AlignRight);
		g->addWidget(sets, 1, 1);
		{
			/* new (a copy to edit), import (an existing theme), check (render it all) */
			QHBoxLayout *bar = new QHBoxLayout;
			QPushButton *bn = new QPushButton("New set\u2026"), *bi = new QPushButton("Import theme\u2026"),
			            *bc = new QPushButton("Check");
			bn->setToolTip("a copy of the selected set under a new name, in ~/.config/themer/templates, to edit");
			bi->setToolTip("turn a theme you have (a GTK theme directory, a .qss, a chrome/ directory...) into part of a set");
			bc->setToolTip("render every file of the selected set for every theme, both modes");
			QObject::connect(bn, &QPushButton::clicked, [this]() { setNew(); });
			QObject::connect(bi, &QPushButton::clicked, [this]() { setImport(); });
			QObject::connect(bc, &QPushButton::clicked, [this]() { withReport("Set check", [](FILE *f) { return set_check(&cfg, cfg.set, f); }); });
			bar->addWidget(bn);
			bar->addWidget(bi);
			bar->addWidget(bc);
			bar->addStretch();
			g->addLayout(bar, 2, 1);
		}
		v->addLayout(g);
		v->addWidget(caption("A template set is the look: the widgets, bars, and chrome every "
		                     "module draws. What a set lacks comes from Themer Default."));
		v->addWidget(heading("Colors"));
		colorsGrid = new QGridLayout;
		colorsGrid->setVerticalSpacing(2);
		colorsGrid->setColumnStretch(4, 1);
		v->addLayout(colorsGrid);
		colorsFill();
		v->addWidget(caption("A color of your own over the scheme's, in both modes: pick one, "
		                     "or Scheme to go back to the scheme's."));
		afterChange = [this]() { QTimer::singleShot(0, [this]() { colorsFill(); }); };
		return w;
	}

	QWidget *pageFonts()
	{
		QVBoxLayout *v;
		QWidget *w = page(v);

		v->addWidget(heading("Fonts"));
		QGridLayout *g = new QGridLayout;
		uiFont = fontButton(cfg.ui_font, sizeof cfg.ui_font, &cfg.ui_size, false);
		monoFont = fontButton(cfg.mono_font, sizeof cfg.mono_font, &cfg.mono_size, true);
		for (int mono = 0; mono < 2; mono++) {
			QSpinBox *sz = new QSpinBox;
			sz->setRange(4, 72);
			sz->setValue(mono ? cfg.mono_size : cfg.ui_size);
			sz->setToolTip("Size in points");
			sz->setKeyboardTracking(false);
			QObject::connect(sz, QOverload<int>::of(&QSpinBox::valueChanged), [mono](int v) {
				if (updating)
					return;
				if (front_set_font(&cfg, mono, mono ? cfg.mono_font : cfg.ui_font, v) == 0)
					changed();
			});
			(mono ? monoSize : uiSize) = sz;
			g->addWidget(new QLabel(mono ? "Terminal" : "Interface"), mono, 0, Qt::AlignRight);
			g->addWidget(mono ? monoFont : uiFont, mono, 1);
			g->addWidget(sz, mono, 2);
		}
		{
			static char names[1024][NAME_LEN];
			int n = front_tty_fonts(names, 1024);
			ttyBox = new QComboBox;
			ttyBox->setEditable(true);
			for (int i = 0; i < n; i++)
				ttyBox->addItem(utf8(names[i]));
			ttyBox->setCurrentText(utf8(cfg.tty_font));
			ttyBox->setToolTip("the Linux console's font (vconsole.conf)");
			QObject::connect(ttyBox, &QComboBox::currentTextChanged, [](const QString &t) {
				if (updating || t == utf8(cfg.tty_font))
					return;
				if (front_set_tty_font(&cfg, t.toUtf8().constData()) == 0)
					changed();
				else if (!t.isEmpty())
					status->setText("a console font name is letters, digits, - and _");
			});
			g->addWidget(new QLabel("Console"), 2, 0, Qt::AlignRight);
			g->addWidget(ttyBox, 2, 1, 1, 2);
		}
		g->setColumnStretch(1, 1);
		v->addLayout(g);
		v->addWidget(caption("The interface font goes to GTK, Qt, the window manager, and the "
		                     "browsers; the terminal font to the terminals, editors, and Qt's "
		                     "fixed font; the console font to the Linux console."));

		v->addWidget(heading("Rendering"));
		g = new QGridLayout;
		hinting = new QComboBox;
		for (int i = 0; i < 4; i++)
			hinting->addItem(hinting_values[i]);
		hinting->setCurrentText(utf8(cfg.hinting));
		QObject::connect(hinting, &QComboBox::currentTextChanged, [](const QString &t) {
			if (updating)
				return;
			if (front_set_hinting(&cfg, t.toUtf8().constData()) == 0)
				changed();
		});
		antialias = new QCheckBox("Antialiasing");
		antialias->setChecked(cfg.antialias);
		QObject::connect(antialias, &QCheckBox::toggled, [](bool on) {
			if (updating)
				return;
			front_set_antialias(&cfg, on);
			changed();
		});
		rgba = new QComboBox;
		for (int i = 0; i < 5; i++)
			rgba->addItem(rgba_values[i]);
		rgba->setCurrentText(utf8(cfg.rgba));
		QObject::connect(rgba, &QComboBox::currentTextChanged, [](const QString &t) {
			if (updating)
				return;
			if (front_set_rgba(&cfg, t.toUtf8().constData()) == 0)
				changed();
		});
		g->addWidget(new QLabel("Hinting"), 0, 0, Qt::AlignRight);
		g->addWidget(hinting, 0, 1);
		g->addWidget(antialias, 1, 1);
		g->addWidget(new QLabel("Subpixel order"), 2, 0, Qt::AlignRight);
		g->addWidget(rgba, 2, 1);
		v->addLayout(g);
		v->addWidget(caption("Written to the GTK settings, xsettingsd, gsettings, and fontconfig, "
		                     "so every toolkit draws text the same way."));
		v->addStretch();
		return w;
	}

	/* the selected icon theme's own icons */
	void fillIcons()
	{
		while (QLayoutItem *it = iconGrid->takeAt(0)) {
			delete it->widget();
			delete it;
		}
		Scheme s;
		gui_scheme(&cfg, &s);
		for (int i = 0; gui_preview_icons[i][0]; i++) {
			QWidget *cell = new QWidget;
			QVBoxLayout *cv = new QVBoxLayout(cell);
			cv->setContentsMargins(0, 0, 0, 0);
			cv->setSpacing(2);
			QLabel *img = new QLabel, *l = new QLabel(utf8(gui_preview_icons[i][1]));
			/* the same lookup the web and GTK previews use: the theme's own file */
			const char *mime;
			size_t len;
			char *data = gui_icon_load(cfg.icons, gui_preview_icons[i][0], ICON_PX, s.hex[C_FG], &len, &mime);
			QPixmap pm;
			if (data) {
				QByteArray bytes(data, (int)len);
				QBuffer buf(&bytes);
				QImageReader rd(&buf);
				QSize sz;
				free(data);
				if (rd.canRead() && (sz = rd.size()).isValid())
					rd.setScaledSize(sz.scaled(ICON_PX, ICON_PX, Qt::KeepAspectRatio));
				pm = QPixmap::fromImage(rd.read());
			}
			if (pm.isNull()) {
				img->setText("none");
				img->setEnabled(false);
			} else {
				img->setPixmap(pm);
			}
			img->setAlignment(Qt::AlignCenter);
			img->setFixedHeight(ICON_PX + 8);
			l->setAlignment(Qt::AlignCenter);
			cv->addWidget(img);
			cv->addWidget(l);
			iconGrid->addWidget(cell, i / 5, i % 5);
		}
	}

	QWidget *pageIcons()
	{
		QVBoxLayout *v;
		QWidget *w = page(v);
		char names[MAX_LIST][NAME_LEN];
		int n = icon_themes_scan(names, MAX_LIST, 0);

		v->addWidget(heading("Icon theme"));
		icons = nameCombo(names, n, cfg.icons);
		QObject::connect(icons, &QComboBox::currentTextChanged, [this](const QString &t) {
			if (updating || t.isEmpty())
				return;
			if (front_set_icons(&cfg, t.toUtf8().constData()) < 0)
				return;
			fillIcons();
			changed();
		});
		v->addWidget(icons);
		v->addWidget(caption("Set for GTK, Qt (qt5ct and qt6ct), xsettingsd, and gsettings. "
		                     "Firefox's toolbar follows it too when the firefox module's icons "
		                     "option is on."));
		QGroupBox *box = new QGroupBox;
		iconGrid = new QGridLayout(box);
		iconGrid->setSpacing(6);
		fillIcons();
		v->addWidget(box);
		v->addStretch();
		return w;
	}

	QWidget *pageCursor()
	{
		QVBoxLayout *v;
		QWidget *w = page(v);
		char names[MAX_LIST][NAME_LEN];
		int n = icon_themes_scan(names, MAX_LIST, 1);

		v->addWidget(heading("Mouse cursor"));
		QGridLayout *g = new QGridLayout;
		cursorBox = nameCombo(names, n, cfg.cursor);
		QObject::connect(cursorBox, &QComboBox::currentTextChanged, [](const QString &t) {
			if (updating || t.isEmpty())
				return;
			if (front_set_cursor(&cfg, t.toUtf8().constData(), 0) == 0)
				changed();
		});
		cursorSize = new QSpinBox;
		cursorSize->setRange(8, 128);
		cursorSize->setSingleStep(2);
		cursorSize->setValue(cfg.cursor_size);
		QObject::connect(cursorSize, QOverload<int>::of(&QSpinBox::valueChanged), [](int px) {
			if (updating)
				return;
			if (front_set_cursor(&cfg, nullptr, px) == 0)
				changed();
		});
		g->addWidget(new QLabel("Theme"), 0, 0, Qt::AlignRight);
		g->addWidget(cursorBox, 0, 1);
		g->addWidget(new QLabel("Size"), 1, 0, Qt::AlignRight);
		g->addWidget(cursorSize, 1, 1);
		v->addLayout(g);
		v->addWidget(caption("Applied through the cursor module (the Xcursor default theme, "
		                     "XCURSOR_THEME and XCURSOR_SIZE, Xresources), the GTK settings, "
		                     "gsettings, and sway's seat. Drawn below at the chosen size."));
		cursorArea = new Cursors;
		v->addWidget(cursorArea);
		v->addStretch();
		return w;
	}

	/* ---- the Server page: themer-web, started and stopped by hand ---- */

	QVBoxLayout *serverBox = nullptr;

	void serverFill()
	{
		while (QLayoutItem *it = serverBox->takeAt(0)) {
			if (it->widget())
				it->widget()->deleteLater();
			if (it->layout()) {
				while (QLayoutItem *k = it->layout()->takeAt(0)) {
					if (k->widget())
						k->widget()->deleteLater();
					delete k;
				}
			}
			delete it;
		}
		int port = front_web_port(&cfg);
		bool up = front_web_running(port);
		QHBoxLayout *row = new QHBoxLayout;
		row->addWidget(new QLabel(up ? "themer-web is running" : "themer-web is stopped"), 1);
		if (up) {
			QLabel *open = new QLabel(QString("<a href=\"http://localhost:%1/\">Open in the browser</a>").arg(port));
			open->setOpenExternalLinks(true);
			row->addWidget(open);
		}
		QPushButton *b = new QPushButton(up ? "Stop" : "Start");
		QObject::connect(b, &QPushButton::clicked, [this]() {
			int port = front_web_port(&cfg);
			bool up = front_web_running(port);
			if ((up ? front_web_stop(port) : front_web_start(port)) < 0)
				status->setText(up ? "could not stop themer-web" : "could not start themer-web (is a browser module on?)");
			else
				status->setText(up ? "themer-web stopped" : "themer-web started");
			QTimer::singleShot(0, [this]() { serverFill(); });
		});
		row->addWidget(b);
		serverBox->addLayout(row);
		QHBoxLayout *prow = new QHBoxLayout;
		QSpinBox *spin = new QSpinBox;
		spin->setRange(1024, 65535);
		spin->setValue(port);
		spin->setKeyboardTracking(false);
		QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), [this](int v) {
			if (front_set_web_port(&cfg, v) < 0)
				status->setText("could not move themer-web to that port");
			else
				status->setText("port saved");
			QTimer::singleShot(0, [this]() { serverFill(); });
		});
		prow->addWidget(new QLabel("Port"));
		prow->addWidget(spin);
		prow->addStretch();
		serverBox->addLayout(prow);
		serverBox->addWidget(caption("On localhost. A running server moves to a new port. It stops by "
		                             "itself when no browser module (firefox, stylus) is on."));
		serverBox->addWidget(caption("Stylus installs the userstyles from it and keeps them current while "
		                             "it runs: each site, with its Install button, is under Modules, stylus."));
	}

	/* empties a layout, the layouts inside it included */
	static void clearLayout(QLayout *l)
	{
		while (QLayoutItem *it = l->takeAt(0)) {
			if (it->widget())
				it->widget()->deleteLater();
			if (it->layout())
				clearLayout(it->layout());
			delete it;
		}
	}

	/* one row per role: the color (yours or the scheme's), its hex, and Scheme to drop yours */
	void colorsFill()
	{
		Scheme s;

		clearLayout(colorsGrid);
		gui_scheme(&cfg, &s);
		for (int r = 0; r < C_N; r++) {
			QPushButton *b = new QPushButton;
			b->setFixedSize(48, 22);
			b->setStyleSheet(QString("QPushButton { background: #%1; border: 1px solid palette(dark); }").arg(s.hex[r]));
			b->setToolTip(cfg.color[r][0] ? "your color" : "the scheme's color: pick one to use your own");
			QObject::connect(b, &QPushButton::clicked, [this, r]() {
				Scheme s;
				gui_scheme(&cfg, &s);
				QColor c = QColorDialog::getColor(hex(s.hex[r]), this, QString("Color: %1").arg(color_names[r]));
				if (!c.isValid())
					return;
				if (front_set_color(&cfg, r, c.name().toUtf8().constData()) == 0)
					changed();
			});
			colorsGrid->addWidget(new QLabel(color_names[r]), r, 0, Qt::AlignRight);
			colorsGrid->addWidget(b, r, 1);
			colorsGrid->addWidget(new QLabel(s.hex[r]), r, 2);
			if (cfg.color[r][0]) {
				QPushButton *x = new QPushButton("Scheme");
				x->setToolTip("back to the scheme's color");
				QObject::connect(x, &QPushButton::clicked, [r]() {
					front_set_color(&cfg, r, nullptr);
					changed();
				});
				colorsGrid->addWidget(x, r, 3);
			} else {
				colorsGrid->addWidget(caption("the scheme's"), r, 3);
			}
		}
	}

	/* the session environment: one row per variable, the value editable */
	void envPut(const char *name, const char *value)
	{
		char msg[160];

		if (front_env_set(&cfg, name, value, msg, sizeof msg) < 0) {
			status->setText(utf8(msg));
			return;
		}
		changed();
		QTimer::singleShot(0, [this]() { envFill(); });
	}

	void envFill()
	{
		clearLayout(envBox);
		QGridLayout *g = new QGridLayout;
		for (int i = 0; i < cfg.nenv; i++) {
			QString name = utf8(cfg.env[i].name);
			QLineEdit *e = new QLineEdit(utf8(cfg.env[i].value));
			QPushButton *x = new QPushButton("Remove");
			QObject::connect(e, &QLineEdit::editingFinished, [this, e, name]() {
				QByteArray n = name.toUtf8(), v = e->text().toUtf8();
				for (int k = 0; k < cfg.nenv; k++)
					if (!strcmp(cfg.env[k].name, n.constData()) && strcmp(cfg.env[k].value, v.constData()))
						envPut(n.constData(), v.constData());
			});
			QObject::connect(x, &QPushButton::clicked, [this, name]() { envPut(name.toUtf8().constData(), ""); });
			g->addWidget(new QLabel(name), i, 0, Qt::AlignRight);
			g->addWidget(e, i, 1);
			g->addWidget(x, i, 2);
		}
		if (!cfg.nenv)
			g->addWidget(caption("No variables."), 0, 0, 1, 2);
		g->setColumnStretch(1, 1);
		envBox->addLayout(g);
	}

	QWidget *pageEnv()
	{
		QVBoxLayout *v;
		QWidget *w = page(v);

		v->addWidget(heading("Environment"));
		v->addWidget(caption("Variables for the whole session, written to environment.d on apply, "
		                     "where login sessions and systemd user services read them."));
		envBox = new QVBoxLayout;
		v->addLayout(envBox);
		envFill();
		QHBoxLayout *add = new QHBoxLayout;
		envName = new QLineEdit;
		envName->setPlaceholderText("NAME");
		envValue = new QLineEdit;
		envValue->setPlaceholderText("value");
		QPushButton *b = new QPushButton("Add");
		auto doAdd = [this]() {
			QByteArray n = envName->text().trimmed().toUtf8(), val = envValue->text().toUtf8();
			if (n.isEmpty() || val.isEmpty()) {
				status->setText("a variable needs a name and a value");
				return;
			}
			envPut(n.constData(), val.constData());
			envName->clear();
			envValue->clear();
		};
		QObject::connect(b, &QPushButton::clicked, doAdd);
		QObject::connect(envValue, &QLineEdit::returnPressed, doAdd);
		add->addWidget(envName);
		add->addWidget(envValue, 1);
		add->addWidget(b);
		v->addLayout(add);
		v->addStretch();
		return w;
	}

	QWidget *pageServer()
	{
		QVBoxLayout *v;
		QWidget *w = page(v);

		v->addWidget(heading("Server"));
		v->addWidget(caption("themer-web: this window's settings in the browser, and the userstyles "
		                     "of the stylus module. Started and stopped here."));
		serverBox = new QVBoxLayout;
		v->addLayout(serverBox);
		serverFill();
		v->addStretch();
		return w;
	}

	QWidget *pageModules()
	{
		QVBoxLayout *v;
		QWidget *w = page(v);

		v->addWidget(heading("Modules"));
		v->addWidget(caption("Each module themes one program: turned on it writes that program's "
		                     "settings, turned off it removes them. Switches and options take "
		                     "effect at once."));
		QHBoxLayout *h = new QHBoxLayout;
		mods = new QTreeWidget;
		mods->setColumnCount(2);
		mods->setHeaderHidden(true);
		mods->setRootIsDecorated(false);
		mods->setFixedWidth(205);
		nmodules = front_module_order(moduleOrder, MAX_MODULES);
		for (int k = 0; k < nmodules; k++) {
			int i = moduleOrder[k];
			QTreeWidgetItem *it = new QTreeWidgetItem(mods, QStringList() << utf8(themer_module_name(i)) << stateText(i));
			it->setData(0, Qt::UserRole, i);
			it->setForeground(1, qApp->palette().color(QPalette::Disabled, QPalette::Text));
		}
		mods->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
		mods->header()->setSectionResizeMode(1, QHeaderView::Stretch);
		h->addWidget(mods);
		detail = new QScrollArea;
		detail->setWidgetResizable(true);
		detail->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);   /* the text wraps instead */
		h->addWidget(detail, 1);
		v->addLayout(h, 1);
		QObject::connect(mods, &QTreeWidget::currentItemChanged, [this](QTreeWidgetItem *it, QTreeWidgetItem *) {
			if (it)
				showModule(it->data(0, Qt::UserRole).toInt());
		});
		if (nmodules)
			mods->setCurrentItem(mods->topLevelItem(0));
		return w;
	}

	/* ordinary widgets, so the Qt stylesheet shows on the real thing */
	QWidget *preview()
	{
		QWidget *w = new QWidget;
		QVBoxLayout *v = new QVBoxLayout(w);
		QTabWidget *tabs = new QTabWidget;

		title = new QLabel;
		title->setEnabled(false);
		v->addWidget(title);

		QWidget *widgets = new QWidget;
		QVBoxLayout *wv = new QVBoxLayout(widgets);
		QMenuBar *bar = new QMenuBar;
		for (const char *m : { "File", "Edit", "View", "Help" }) {
			QMenu *menu = bar->addMenu(m);
			menu->addAction("New");
			menu->addAction("Open...");
			menu->addAction("Save");
			menu->addSeparator();
			menu->addAction("Quit");
		}
		wv->setMenuBar(bar);
		QHBoxLayout *row = new QHBoxLayout;
		row->addWidget(new QPushButton("Button"));
		QPushButton *toggled = new QPushButton("Toggled");
		toggled->setCheckable(true);
		toggled->setChecked(true);
		row->addWidget(toggled);
		QPushButton *disabled = new QPushButton("Disabled");
		disabled->setEnabled(false);
		row->addWidget(disabled);
		QToolButton *tool = new QToolButton;
		tool->setIcon(QIcon::fromTheme("document-open"));
		row->addWidget(tool);
		row->addStretch();
		wv->addLayout(row);
		row = new QHBoxLayout;
		row->addWidget(new QLineEdit("a text field"), 1);
		QSpinBox *spin = new QSpinBox;
		spin->setValue(42);
		row->addWidget(spin);
		wv->addLayout(row);
		row = new QHBoxLayout;
		QCheckBox *checked = new QCheckBox("checked");
		checked->setChecked(true);
		row->addWidget(checked);
		row->addWidget(new QCheckBox("unchecked"));
		QRadioButton *r1 = new QRadioButton("radio");
		r1->setChecked(true);
		row->addWidget(r1);
		row->addWidget(new QRadioButton("other"));
		row->addStretch();
		wv->addLayout(row);
		QSlider *slider = new QSlider(Qt::Horizontal);
		slider->setValue(40);
		wv->addWidget(slider);
		QProgressBar *pbar = new QProgressBar;
		pbar->setValue(60);
		wv->addWidget(pbar);
		QComboBox *combo = new QComboBox;
		combo->addItem("a combo box");
		combo->addItem("another choice");
		wv->addWidget(combo);
		QTextEdit *text = new QTextEdit;
		text->setPlainText("A text edit. The quick brown fox jumps over the lazy dog.\n"
		                   "0123456789 \xe2\x80\x94 how the interface font renders.");
		wv->addWidget(text, 1);
		tabs->addTab(widgets, "Widgets");

		QTreeWidget *tree = new QTreeWidget;
		tree->setColumnCount(2);
		tree->setHeaderLabels(QStringList() << "Folder" << "Unread");
		const char *rows[][2] = { { "Inbox", "12" }, { "Drafts", "1" }, { "Sent", "" }, { "Archive", "" } };
		for (auto &r : rows)
			new QTreeWidgetItem(tree, QStringList() << r[0] << r[1]);
		tree->setCurrentItem(tree->topLevelItem(0));
		tabs->addTab(tree, "List");

		sample = new Sample;
		tabs->addTab(sample, "Terminal");
		v->addWidget(tabs, 1);

		swatches = new Swatches;
		swatches->setMinimumHeight(80);
		v->addWidget(swatches);
		return w;
	}
};

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);

	if (themer_startup(&cfg) < 0)
		return 1;
	applied = cfg;
	front_track(&cfg, &applied);
	app.setStyle("Fusion");
	{
		/* icon themes from everywhere GTK looks, ~/.icons included */
		QStringList paths = QIcon::themeSearchPaths();
		paths.prepend(QDir::homePath() + "/.icons");
		paths << "/usr/share/pixmaps";
		paths.removeDuplicates();
		QIcon::setThemeSearchPaths(paths);
	}
	updating = true;
	Window w;
	QTimer sync;
	QObject::connect(&sync, &QTimer::timeout, [&w]() { w.syncTick(); });
	sync.start(1000);
	updating = false;
	restyle();
	if (argc > 1)
		w.openPage(argv[1], argc > 2 ? argv[2] : nullptr);
	w.show();
	return app.exec();
}
