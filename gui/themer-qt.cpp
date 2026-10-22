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
#include <QIcon>
#include <QImage>
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
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <cstdio>
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
static char theme_names[MAX_LIST][NAME_LEN];
static Theme themes[MAX_LIST];
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
	static char qss[65536];
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
	if (front_render(&cfg, "qt", "qt", "qt.qss", qss, sizeof qss) == 0)
		qApp->setStyleSheet(utf8(qss));
	if (title)
		title->setText(utf8(cfg.th.name) + "  \xc2\xb7  " + (cfg.mode == MODE_DARK ? "dark" : "light") +
		               "  \xc2\xb7  " + utf8(cfg.set));
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

static void changed()
{
	if (updating)
		return;
	markDirty();
	restyle();
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
		resize(980, 640);
		menus();

		QWidget *central = new QWidget;
		QVBoxLayout *v = new QVBoxLayout(central);
		v->setContentsMargins(0, 0, 0, 0);
		QSplitter *split = new QSplitter;
		split->addWidget(settings());
		split->addWidget(preview());
		split->setStretchFactor(1, 1);
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
	void openPage(const char *name)
	{
		static const char *pages[] = { "theme", "fonts", "icons", "cursor", "modules" };
		for (int i = 0; i < 5; i++)
			if (!strcmp(pages[i], name))
				pageList->setCurrentRow(i);
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
	QRadioButton *dark = nullptr, *light = nullptr;
	QComboBox *sets = nullptr, *icons = nullptr, *cursorBox = nullptr, *hinting = nullptr, *rgba = nullptr;
	QPushButton *uiFont = nullptr, *monoFont = nullptr;
	QSpinBox *cursorSize = nullptr;
	QCheckBox *antialias = nullptr;
	QListWidget *mods = nullptr;
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
		updating = true;
		for (int i = 0; i < ntheme_names; i++)
			if (!strcmp(theme_names[i], cfg.th.name))
				themeList->setCurrentRow(i);
		(cfg.mode == MODE_DARK ? dark : light)->setChecked(true);
		sets->setCurrentText(utf8(cfg.set));
		fontLabel(uiFont, cfg.ui_font, cfg.ui_size);
		fontLabel(monoFont, cfg.mono_font, cfg.mono_size);
		icons->setCurrentText(utf8(cfg.icons));
		cursorBox->setCurrentText(utf8(cfg.cursor));
		cursorSize->setValue(cfg.cursor_size);
		hinting->setCurrentText(utf8(cfg.hinting));
		antialias->setChecked(cfg.antialias);
		rgba->setCurrentText(utf8(cfg.rgba));
		for (int k = 0; k < nmodules; k++)
			mods->item(k)->setCheckState(themer_module_on(&cfg, moduleOrder[k]) ? Qt::Checked : Qt::Unchecked);
		updating = false;
		dirty = false;
		applyButton->setEnabled(false);
		status->setText("back to the applied settings");
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
		b->setText(utf8(fam) + "  " + QString::number(size));
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
			if (front_set_font(&cfg, mono, f.family().toUtf8().constData(),
			                   f.pointSize() > 0 ? f.pointSize() : 0) < 0)
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
		pageList->addItems(QStringList() << "Theme" << "Fonts" << "Icons" << "Cursor" << "Modules");
		pageList->setFixedWidth(130);
		pageList->setFrameShape(QFrame::NoFrame);
		pages = new QStackedWidget;
		pages->addWidget(pageTheme());
		pages->addWidget(pageFonts());
		pages->addWidget(pageIcons());
		pages->addWidget(pageCursor());
		pages->addWidget(pageModules());
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
		char names[MAX_LIST][NAME_LEN];
		int n = template_all_sets(names, MAX_LIST);

		updating = true;
		sets->clear();
		for (int i = 0; i < n; i++)
			sets->addItem(utf8(names[i]));
		sets->setCurrentText(utf8(cfg.set));
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

	/* ---- module options: the selected module's options, set at once ---- */

	QWidget *optionsPane = nullptr;
	QFormLayout *optionsForm = nullptr;
	QLabel *optionsTitle = nullptr;

	void showOptions(int module)
	{
		while (QLayoutItem *it = optionsForm->takeAt(0)) {
			delete it->widget();
			delete it;
		}
		int n = themer_option_count(module);
		optionsTitle->setText(QString("<b>%1</b> options%2").arg(utf8(themer_module_name(module)))
		                      .arg(n ? "" : ": none"));
		for (int j = 0; j < n; j++) {
			const TmOption *o = themer_option(module, j);
			const char *cur = themer_option_value(&cfg, module, j);
			QWidget *w;
			auto set = [module, j, this](const QString &value) {
				char msg[256];
				QByteArray b = value.toUtf8();
				if (updating || b == themer_option_value(&cfg, module, j))
					return;
				front_option_set(&cfg, module, j, b.constData(), msg, sizeof msg);
				status->setText(utf8(msg));
				restyle();
			};
			if (!strcmp(o->name, "template")) {
				QComboBox *c = new QComboBox;
				char names[MAX_LIST][NAME_LEN];
				int ns = template_all_sets(names, MAX_LIST);
				for (int i = 0; i < ns; i++)
					c->addItem(utf8(names[i]));
				c->setCurrentText(utf8(cur));
				QObject::connect(c, &QComboBox::currentTextChanged, set);
				w = c;
			} else if (o->choices) {
				QComboBox *c = new QComboBox;
				for (const QString &s : utf8(o->choices).split('|'))
					c->addItem(s);
				c->setCurrentText(utf8(cur));
				QObject::connect(c, &QComboBox::currentTextChanged, set);
				w = c;
			} else if (o->max > o->min) {
				QSpinBox *s = new QSpinBox;
				s->setRange(o->min, o->max);
				s->setValue(atoi(cur));
				QObject::connect(s, QOverload<int>::of(&QSpinBox::valueChanged), [set](int v) { set(QString::number(v)); });
				w = s;
			} else {
				QLineEdit *e = new QLineEdit(utf8(cur));
				QObject::connect(e, &QLineEdit::editingFinished, [set, e]() { set(e->text()); });
				w = e;
			}
			w->setToolTip(utf8(o->desc));
			QLabel *l = new QLabel(utf8(o->name));
			l->setToolTip(utf8(o->desc));
			optionsForm->addRow(l, w);
		}
	}

	QWidget *pageTheme()
	{
		QVBoxLayout *v;
		QWidget *w = page(v);
		char names[MAX_LIST][NAME_LEN];
		int n;

		v->addWidget(heading("Color theme"));
		themeList = new QListWidget;
		themeList->setIconSize(QSize(40, 18));
		ntheme_names = theme_list(theme_names, MAX_LIST);
		for (int i = 0; i < ntheme_names; i++) {
			theme_read(theme_names[i], &themes[i]);
			themeList->addItem(new QListWidgetItem(themeSwatch(themes[i], cfg.mode == MODE_DARK ? 0 : 1),
			                                       utf8(theme_names[i])));
			if (!strcmp(theme_names[i], cfg.th.name))
				themeList->setCurrentRow(i);
		}
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

		QGridLayout *g = new QGridLayout;
		QHBoxLayout *mode = new QHBoxLayout;
		dark = new QRadioButton("Dark");
		light = new QRadioButton("Light");
		(cfg.mode == MODE_DARK ? dark : light)->setChecked(true);
		QObject::connect(dark, &QRadioButton::toggled, [](bool on) { if (on && !updating) { cfg.mode = MODE_DARK; changed(); } });
		QObject::connect(light, &QRadioButton::toggled, [](bool on) { if (on && !updating) { cfg.mode = MODE_LIGHT; changed(); } });
		mode->addWidget(dark);
		mode->addWidget(light);
		mode->addStretch();
		g->addWidget(new QLabel("Mode"), 0, 0, Qt::AlignRight);
		g->addLayout(mode, 0, 1);
		sets = new QComboBox;
		n = template_all_sets(names, MAX_LIST);
		for (int i = 0; i < n; i++)
			sets->addItem(utf8(names[i]));
		sets->setCurrentText(utf8(cfg.set));
		QObject::connect(sets, &QComboBox::currentTextChanged, [](const QString &t) {
			if (updating || t.isEmpty())
				return;
			if (front_set_set(&cfg, t.toUtf8().constData()) == 0)
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
		                     "module draws. What a set lacks comes from themer_chicago."));
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
		g->addWidget(new QLabel("Interface"), 0, 0, Qt::AlignRight);
		g->addWidget(uiFont, 0, 1);
		g->addWidget(new QLabel("Terminal"), 1, 0, Qt::AlignRight);
		g->addWidget(monoFont, 1, 1);
		v->addLayout(g);
		v->addWidget(caption("The interface font goes to GTK, Qt, the window manager, and the "
		                     "browsers; the terminal font to the terminals, editors, and Qt's "
		                     "fixed font."));

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
		QIcon::setThemeName(utf8(cfg.icons));
		for (int i = 0; gui_preview_icons[i][0]; i++) {
			QWidget *cell = new QWidget;
			QVBoxLayout *cv = new QVBoxLayout(cell);
			cv->setContentsMargins(0, 0, 0, 0);
			cv->setSpacing(2);
			QLabel *img = new QLabel, *l = new QLabel(utf8(gui_preview_icons[i][1]));
			QIcon ic = QIcon::fromTheme(utf8(gui_preview_icons[i][0]));
			QPixmap pm = ic.pixmap(ICON_PX, ICON_PX);
			if (pm.isNull()) {
				/* newer themes keep many icons only in symbolic form; those are
				 * single-color and get the text color, as GTK would paint them */
				ic = QIcon::fromTheme(utf8(gui_preview_icons[i][0]) + "-symbolic");
				pm = ic.pixmap(ICON_PX, ICON_PX);
				if (!pm.isNull()) {
					Scheme s;
					QPainter pr(&pm);
					gui_scheme(&cfg, &s);
					pr.setCompositionMode(QPainter::CompositionMode_SourceIn);
					pr.fillRect(pm.rect(), hex(s.hex[C_FG]));
				}
			}
			img->setPixmap(pm);
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

	QWidget *pageModules()
	{
		QVBoxLayout *v;
		QWidget *w = page(v);

		v->addWidget(heading("Modules"));
		v->addWidget(caption("Each module writes one program's settings on Apply and removes "
		                     "them when turned off. Select a module for its options, which take "
		                     "effect at once; \"template\" pins it to a set of its own (the global "
		                     "set otherwise)."));
		mods = new QListWidget;
		mods->setWordWrap(true);
		nmodules = front_module_order(moduleOrder, MAX_MODULES);
		for (int k = 0; k < nmodules; k++) {
			int i = moduleOrder[k];
			bool here = themer_module_present(&cfg, i);
			QListWidgetItem *it = new QListWidgetItem(utf8(themer_module_name(i)) +
			                                          (here ? "" : "  (not installed)") + "\n" +
			                                          utf8(themer_module_desc(i)), mods);
			if (!here)
				it->setForeground(qApp->palette().color(QPalette::Disabled, QPalette::Text));
			it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
			it->setCheckState(themer_module_on(&cfg, i) ? Qt::Checked : Qt::Unchecked);
			it->setData(Qt::UserRole, i);
		}
		QObject::connect(mods, &QListWidget::itemChanged, [](QListWidgetItem *it) {
			if (updating)
				return;
			int i = it->data(Qt::UserRole).toInt();
			config_set_module(&cfg, themer_module_name(i), it->checkState() == Qt::Checked);
			markDirty();
		});
		v->addWidget(mods, 1);
		optionsPane = new QGroupBox;
		QVBoxLayout *ov = new QVBoxLayout(optionsPane);
		optionsTitle = new QLabel("select a module for its options");
		ov->addWidget(optionsTitle);
		optionsForm = new QFormLayout;
		ov->addLayout(optionsForm);
		v->addWidget(optionsPane);
		QObject::connect(mods, &QListWidget::currentItemChanged, [this](QListWidgetItem *it, QListWidgetItem *) {
			if (it)
				showOptions(it->data(Qt::UserRole).toInt());
		});
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
	updating = false;
	restyle();
	if (argc > 1)
		w.openPage(argv[1]);
	w.show();
	return app.exec();
}
