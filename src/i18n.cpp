#include "i18n.h"
#include "library.h"
#include <QCoreApplication>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace I18n {
namespace {
Language state = Chinese;
QTranslator* active = nullptr;

QString settingsPath() {
    try { return Cam::settingsFile(); }
    catch(...) { return {}; }
}
}

Language language() { return state; }

Language parse(const QString& text) {
    const QString value = text.trimmed().toLower();
    if (value.startsWith("en")) return English;
    if (value.startsWith("zh")) return Chinese;
    return QLocale::system().language() == QLocale::English ? English : Chinese;
}
QString name(Language value) { return value == English ? "en" : "zh"; }

void install(Language value) {
    state = value;
    auto* application = QCoreApplication::instance();
    if (!application) return;
    // Month and weekday names come from the locale, not from the translation.
    QLocale::setDefault(value == English ? QLocale(QLocale::English, QLocale::UnitedStates)
                                         : QLocale(QLocale::Chinese, QLocale::China));
    if (active) { QCoreApplication::removeTranslator(active); delete active; active = nullptr; }
    if (value != English) return;
    auto* translator = new QTranslator(application);
    if (translator->load(":/i18n/teslacam_en.qm")) {
        QCoreApplication::installTranslator(translator);
        active = translator;
    } else delete translator;
}
void save(Language value) {
    const QString path = settingsPath();
    if (path.isEmpty()) return;
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue("appearance/language", name(value));
}
Language load() {
    const QString path = settingsPath();
    if (!path.isEmpty()) {
        QSettings settings(path, QSettings::IniFormat);
        const QString saved = settings.value("appearance/language").toString();
        if (!saved.isEmpty()) return parse(saved);
    }
    return QLocale::system().language() == QLocale::English ? English : Chinese;
}
}
