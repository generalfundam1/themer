/* themerqpa.cpp: themer's own Qt platform theme, so Qt programs take the theme
 * without qt5ct or qt6ct. Built once against Qt 5 and once against Qt 6 (the same
 * source), installed as platformthemes/libthemer.so, selected with
 * QT_QPA_PLATFORMTHEME=themer (the qt module's environment file sets it).
 *
 * It reads what the qt module writes: ~/.config/themer/qt/theme.conf (the style,
 * the icon theme, the fonts, the palette as role lists) and the rendered
 * stylesheet beside it. The palette and fonts go through QPlatformTheme, which
 * is how Qt asks a platform for them; the stylesheet is set on the application
 * once it exists. Both files are watched: an apply in themer restyles running
 * Qt programs at once, no restart.
 *
 * No moc: the plugin's metadata (the CBOR blob Qt's loader reads, what moc would
 * generate from Q_PLUGIN_METADATA) is written out by hand below, and the plugin
 * class needs no signals or slots. */
#include <qpa/qplatformtheme.h>
#include <qpa/qplatformthemeplugin.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QPointer>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileSystemWatcher>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtCore/QVariant>
#include <QtGui/QFont>
#include <QtGui/QGuiApplication>
#include <QtGui/QIcon>
#include <QtGui/QPalette>
#include <QtWidgets/QApplication>
#include <QtWidgets/QStyle>
#include <QtWidgets/QStyleFactory>

QT_USE_NAMESPACE

class ThemerTheme : public QPlatformTheme
{
public:
	ThemerTheme()
	{
		m_dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/themer/qt";
		load();
		/* the application does not exist yet when the platform theme is made: the
		 * stylesheet and the watcher wait for it */
		QTimer::singleShot(0, [this]() { afterStart(); });
	}

	const QPalette *palette(Palette type = SystemPalette) const override
	{
		Q_UNUSED(type);
		return m_havePalette ? &m_palette : nullptr;
	}

	const QFont *font(Font type = SystemFont) const override
	{
		/* only the two Qt asks every program for: a font for each widget class as
		 * well would fill QApplication's per-class font table, and widgets styled
		 * by a stylesheet then keep those instead of following the application font */
		switch (type) {
		case SystemFont:
			return &m_font;
		case FixedFont:
			return &m_fixed;
		default:
			return nullptr;
		}
	}

	QVariant themeHint(ThemeHint hint) const override
	{
		switch (hint) {
		case SystemIconThemeName:
			return m_icons;
		case SystemIconFallbackThemeName:
			return QStringLiteral("hicolor");
		case IconThemeSearchPaths:
			return iconSearchPaths();
		case StyleNames:
			return QStringList() << m_style;
		case DialogButtonBoxButtonsHaveIcons:
			return false;
		case UiEffects:
			return 0;
		default:
			return QPlatformTheme::themeHint(hint);
		}
	}

private:
	QString m_dir, m_style, m_icons, m_qss;

	/* where GTK looks for icon themes, ~/.icons included: Qt's own default leaves
	 * that one out, so a theme installed there showed no icons in Qt programs */
	static QStringList iconSearchPaths()
	{
		QStringList paths;
		paths << QDir::homePath() + "/.icons";
		for (const QString &d : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
			paths << d + "/icons";
		paths << "/usr/share/pixmaps";
		paths.removeDuplicates();
		return paths;
	}
	QPalette m_palette;
	/* Not default-constructed: QFont() asks QGuiApplication for its font, and that
	 * happens while this theme is still being created, so Qt would settle on the
	 * platform default before it could ask the theme for SystemFont. */
	QFont m_font{QString(), -1}, m_fixed{QString(), -1};
	bool m_havePalette = false;
	QFileSystemWatcher *m_watch = nullptr;

	/* the 21 roles of one color group, in QPalette::ColorRole order, "#rrggbb, ..." */
	void setGroup(QPalette::ColorGroup g, const QString &list)
	{
		const QStringList colors = list.split(',', Qt::SkipEmptyParts);
		for (int i = 0; i < colors.size() && i < QPalette::NColorRoles; i++) {
			QColor c(colors.at(i).trimmed());
			if (c.isValid())
				m_palette.setColor(g, QPalette::ColorRole(i), c);
		}
	}

	void load()
	{
		QSettings s(m_dir + "/theme.conf", QSettings::IniFormat);
		s.beginGroup("themer");
		m_style = s.value("style", "Fusion").toString();
		m_icons = s.value("icon_theme", "Adwaita").toString();
		m_font = QFont(s.value("font", "sans-serif").toString(), s.value("font_size", 10).toInt());
		m_fixed = QFont(s.value("fixed_font", "monospace").toString(), s.value("fixed_size", 10).toInt());
		m_fixed.setStyleHint(QFont::Monospace);
		/* the rendering settings, the same ones GTK and fontconfig get */
		{
			QString h = s.value("hinting", "slight").toString();
			QFont::HintingPreference hp = h == "none" ? QFont::PreferNoHinting
			                            : h == "full" ? QFont::PreferFullHinting
			                            : h == "medium" ? QFont::PreferDefaultHinting : QFont::PreferVerticalHinting;
			QFont::StyleStrategy ss = s.value("antialias", 1).toInt() ? QFont::PreferAntialias : QFont::NoAntialias;
			m_font.setHintingPreference(hp);
			m_fixed.setHintingPreference(hp);
			m_font.setStyleStrategy(ss);
			m_fixed.setStyleStrategy(ss);
		}
		m_qss = s.value("stylesheet").toString();
		m_palette = QPalette();
		m_havePalette = s.contains("active");
		if (m_havePalette) {
			/* QSettings reads a comma-separated value as a list: join it back */
			setGroup(QPalette::Active, s.value("active").toStringList().join(','));
			setGroup(QPalette::Inactive, s.value("inactive", s.value("active")).toStringList().join(','));
			setGroup(QPalette::Disabled, s.value("disabled", s.value("active")).toStringList().join(','));
		}
	}

	QString stylesheet() const
	{
		QFile f(m_qss);
		if (m_qss.isEmpty() || !f.open(QIODevice::ReadOnly))
			return QString();
		return QString::fromUtf8(f.readAll());
	}

	/* everything a running application can take: the palette, the fonts, the
	 * icon theme, the style, the stylesheet */
	void apply()
	{
		QGuiApplication *g = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
		if (!g)
			return;
		QIcon::setThemeName(m_icons);
		g->setFont(m_font);
		if (m_havePalette)
			g->setPalette(m_palette);
		if (QApplication *a = qobject_cast<QApplication *>(g)) {
			if (!a->style() || a->style()->objectName().compare(m_style, Qt::CaseInsensitive) != 0)
				if (QStyle *st = QStyleFactory::create(m_style))
					a->setStyle(st);
			a->setStyleSheet(stylesheet());
		}
	}

	void afterStart()
	{
		apply();
		if (!QCoreApplication::instance())
			return;
		m_watch = new QFileSystemWatcher(QCoreApplication::instance());
		watchFiles();
		QObject::connect(m_watch, &QFileSystemWatcher::fileChanged, [this](const QString &) { changed(); });
		QObject::connect(m_watch, &QFileSystemWatcher::directoryChanged, [this](const QString &) { changed(); });
	}

	void watchFiles()
	{
		QStringList paths;
		paths << m_dir;
		if (QFile::exists(m_dir + "/theme.conf"))
			paths << m_dir + "/theme.conf";
		if (!m_qss.isEmpty() && QFile::exists(m_qss))
			paths << m_qss;
		m_watch->addPaths(paths);
	}

	/* themer writes the files anew (an editor-style replace), so the watch on the
	 * old inode is gone: re-add after a short settle, then apply */
	void changed()
	{
		QTimer::singleShot(150, [this]() {
			load();
			if (!m_watch->files().isEmpty())
				m_watch->removePaths(m_watch->files());
			watchFiles();
			apply();
		});
	}
};

class ThemerPlugin : public QPlatformThemePlugin
{
public:
	QPlatformTheme *create(const QString &key, const QStringList &params) override
	{
		Q_UNUSED(params);
		if (key.compare(QLatin1String("themer"), Qt::CaseInsensitive) == 0)
			return new ThemerTheme;
		return nullptr;
	}
};

/* ---- the plugin metadata, as moc would write it ----
 *
 * "QTMETADATA !" then four bytes (metadata version 0, the Qt major and minor the
 * plugin was built for, the architecture requirements), then a CBOR map:
 *   2 -> the interface ID, 3 -> the class name, 4 -> { "Keys": [ "themer" ] }
 * Qt's loader reads this from the .qtmetadata section without loading the library,
 * then through qt_plugin_query_metadata (Qt 5) / qt_plugin_query_metadata_v2 (Qt 6). */
#define IID "org.qt-project.Qt.QPA.QPlatformThemeFactoryInterface.5.1"

__attribute__((section(".qtmetadata"))) __attribute__((used))
static const unsigned char qt_pluginMetaData[] = {
	'Q', 'T', 'M', 'E', 'T', 'A', 'D', 'A', 'T', 'A', ' ', '!',
	0, QT_VERSION_MAJOR, QT_VERSION_MINOR, 0,
	0xa3,                                   /* map of 3 */
	0x02, 0x78, sizeof(IID) - 1,            /* 2: text(57) */
	'o','r','g','.','q','t','-','p','r','o','j','e','c','t','.','Q','t','.','Q','P','A','.',
	'Q','P','l','a','t','f','o','r','m','T','h','e','m','e','F','a','c','t','o','r','y',
	'I','n','t','e','r','f','a','c','e','.','5','.','1',
	0x03, 0x6c,                             /* 3: text(12) */
	'T','h','e','m','e','r','P','l','u','g','i','n',
	0x04, 0xa1,                             /* 4: map of 1 */
	0x64, 'K','e','y','s',                  /*   "Keys": */
	0x81, 0x66, 't','h','e','m','e','r',    /*   [ "themer" ] */
};

extern "C" {
Q_DECL_EXPORT QObject *qt_plugin_instance()
{
	static QPointer<QObject> instance;
	if (!instance)
		instance = new ThemerPlugin;
	return instance;
}
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
Q_DECL_EXPORT QPluginMetaData qt_plugin_query_metadata_v2()
{
	return { qt_pluginMetaData, sizeof qt_pluginMetaData };
}
#else
Q_DECL_EXPORT const char *qt_plugin_query_metadata()
{
	return reinterpret_cast<const char *>(qt_pluginMetaData);
}
#endif
}
