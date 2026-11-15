#include "theme.h"
#include "library.h"
#include <QApplication>
#include <QHash>
#include <QSettings>
#include <QStyleHints>

namespace Theme {
namespace {
struct Tokens {
    QHash<QString, QString> values;
    Tokens(std::initializer_list<std::pair<const char*, const char*>> list) {
        for (const auto& item : list) values.insert(QString::fromLatin1(item.first), QString::fromLatin1(item.second));
    }
};

// Light stays on neutral greys so the teal accent is the only strong colour.
const Tokens& light() {
    static const Tokens table{
        {"bg", "#f4f5f6"}, {"sidebar", "#ebedef"}, {"surface", "#ffffff"},
        {"surfaceAlt", "#f8f9fa"}, {"headerBg", "#f0f2f3"},
        {"border", "#d7dbdf"}, {"borderSoft", "#e6e9ec"},
        {"text", "#23282d"}, {"title", "#1a1f24"}, {"muted", "#6b7480"},
        {"disabled", "#a3abb4"}, {"disabledBg", "#f1f3f4"}, {"hover", "#eef1f3"},
        {"accent", "#147d72"}, {"accentHover", "#10685f"}, {"accentText", "#ffffff"},
        {"accentSoft", "#d8eee9"}, {"accentSoftText", "#123c37"}, {"accentBorder", "#7fb9b1"},
        {"primaryDisabledBg", "#e2e6e8"},
        {"danger", "#b53e45"}, {"dangerBorder", "#e4c4c7"},
        {"track", "#dde1e5"},
        {"videoBg", "#171d24"}, {"videoTitle", "#d3e2ec"}, {"videoStatus", "#a9bcc9"},
        {"highlightBg", "#d8eee9"}, {"highlightText", "#0a6f60"},
        {"spanKnown", "#147d72"}, {"spanEstimate", "#a9cfc8"}
    };
    return table;
}
const Tokens& dark() {
    static const Tokens table{
        {"bg", "#15191d"}, {"sidebar", "#1c2229"}, {"surface", "#222a32"},
        {"surfaceAlt", "#262e37"}, {"headerBg", "#2a323b"},
        {"border", "#36414c"}, {"borderSoft", "#2c353f"},
        {"text", "#dfe5ea"}, {"title", "#f0f3f6"}, {"muted", "#97a3ae"},
        {"disabled", "#5f6b77"}, {"disabledBg", "#232a32"}, {"hover", "#2a333c"},
        {"accent", "#3aa89b"}, {"accentHover", "#48b8aa"}, {"accentText", "#0f1519"},
        {"accentSoft", "#21403c"}, {"accentSoftText", "#b6e5dc"}, {"accentBorder", "#3f7a73"},
        {"primaryDisabledBg", "#2a333c"},
        {"danger", "#e0797f"}, {"dangerBorder", "#5a3438"},
        {"track", "#2b343d"},
        {"videoBg", "#0e1216"}, {"videoTitle", "#cfdce6"}, {"videoStatus", "#93a5b2"},
        {"highlightBg", "#21403c"}, {"highlightText", "#7fd3c6"},
        {"spanKnown", "#3aa89b"}, {"spanEstimate", "#3d5f5b"}
    };
    return table;
}
Mode state = Light;
const Tokens& table() { return state == Dark ? dark() : light(); }

QString settingsPath() {
    try { return Cam::settingsFile(); }
    catch(...) { return {}; }
}

// Widget rules reference @{token}; braces keep @{accent} from matching @{accentHover}.
const char* styleTemplate = R"(
QWidget { font-family: 'Microsoft YaHei UI'; font-size: 12px; color: @{text}; }
QMainWindow, QMainWindow > QWidget { background: @{bg}; }
QLabel#title { font-size: 25px; font-weight: 700; color: @{title}; }
QLabel#section { font-size: 13px; font-weight: 700; }
QLabel#muted { color: @{muted}; font-size: 12px; }
QLabel#emptyState { color: @{muted}; font-size: 14px; }
QToolButton { background: @{surface}; border: 1px solid @{border}; border-radius: 6px; padding: 8px 11px; }
QToolButton#iconButton { background: transparent; border: 1px solid transparent; border-radius: 4px; color: @{muted}; font-size: 17px; padding: 2px 5px; }
QToolButton#iconButton:hover { color: @{accent}; }
QToolButton#iconButton:focus { background: transparent; border-color: @{accent}; color: @{accent}; }
QToolButton:focus, QPushButton:focus { border: 2px solid @{accent}; }
QWidget#sidebar { background: @{sidebar}; border-radius: 10px; }
QPushButton { background: @{surface}; border: 1px solid @{border}; border-radius: 6px; padding: 8px 11px; }
QPushButton:hover { background: @{hover}; border-color: @{accentBorder}; }
QPushButton:disabled { color: @{disabled}; background: @{disabledBg}; }
QPushButton#primary { background: @{accent}; color: @{accentText}; border-color: @{accent}; font-weight: 600; }
QPushButton#primary:hover { background: @{accentHover}; }
QPushButton#primary:disabled { background: @{primaryDisabledBg}; color: @{disabled}; border-color: @{primaryDisabledBg}; }
QPushButton#danger { color: @{danger}; border-color: @{dangerBorder}; }
QComboBox, QLineEdit, QDateTimeEdit, QSpinBox { background: @{surface}; border: 1px solid @{border}; border-radius: 5px; padding: 6px; min-height: 19px; }
QComboBox QAbstractItemView { background: @{surface}; color: @{text}; border: 1px solid @{border}; selection-background-color: @{accentSoft}; selection-color: @{accentSoftText}; }
QTableWidget { background: @{surface}; alternate-background-color: @{surfaceAlt}; border: 1px solid @{borderSoft}; border-radius: 7px; selection-background-color: @{accentSoft}; selection-color: @{accentSoftText}; }
QHeaderView::section { background: @{headerBg}; border: none; padding: 9px 4px; color: @{muted}; }
QTableWidget::item { padding: 3px; border-bottom: 1px solid @{borderSoft}; }
QWidget#cameraBox { background: @{videoBg}; border-radius: 7px; }
QLabel#cameraTitle { color: @{videoTitle}; font-weight: 600; }
QLabel#cameraStatus { color: @{videoStatus}; }
QWidget#rangePanel { background: @{sidebar}; border-radius: 7px; }
QProgressBar { background: @{track}; border: none; border-radius: 3px; text-align: center; }
QProgressBar::chunk { background: @{accent}; border-radius: 3px; }
QCalendarWidget { background: @{surface}; }
QCalendarWidget QWidget#qt_calendar_navigationbar { background: transparent; }
QCalendarWidget QToolButton { color: @{text}; background: transparent; border: none; border-radius: 4px; padding: 3px 5px; font-size: 12px; font-weight: 600; }
QCalendarWidget QToolButton:hover { background: @{hover}; }
QCalendarWidget QToolButton:pressed { background: @{accentSoft}; color: @{accentSoftText}; }
QCalendarWidget QToolButton::menu-indicator { image: none; }
QCalendarWidget QMenu { background: @{surface}; border: 1px solid @{border}; padding: 4px; }
QCalendarWidget QMenu::item { padding: 4px 14px; border-radius: 4px; }
QCalendarWidget QMenu::item:selected { background: @{accentSoft}; color: @{accentSoftText}; }
QCalendarWidget QSpinBox { background: @{surface}; border: 1px solid @{border}; border-radius: 4px; padding: 1px 3px; }
QCalendarWidget QAbstractItemView { background: @{surface}; border: none; outline: none; color: @{text}; font-size: 11px; selection-background-color: @{accent}; selection-color: @{accentText}; }
QCalendarWidget QAbstractItemView:disabled { color: @{disabled}; }
QSplitter::handle { background: transparent; width: 6px; }
)";

QString expand(const QString& text) {
    QString result = text;
    const auto& values = table().values;
    for (auto it = values.cbegin(); it != values.cend(); ++it) result.replace("@{" + it.key() + "}", it.value());
    return result;
}

QPalette buildPalette() {
    QPalette palette;
    palette.setColor(QPalette::Window, color("bg"));
    palette.setColor(QPalette::WindowText, color("text"));
    palette.setColor(QPalette::Base, color("surface"));
    palette.setColor(QPalette::AlternateBase, color("surfaceAlt"));
    palette.setColor(QPalette::Text, color("text"));
    palette.setColor(QPalette::Button, color("surface"));
    palette.setColor(QPalette::ButtonText, color("text"));
    palette.setColor(QPalette::BrightText, color("title"));
    palette.setColor(QPalette::Highlight, color("accent"));
    palette.setColor(QPalette::HighlightedText, color("accentText"));
    palette.setColor(QPalette::ToolTipBase, color("surface"));
    palette.setColor(QPalette::ToolTipText, color("text"));
    palette.setColor(QPalette::PlaceholderText, color("disabled"));
    palette.setColor(QPalette::Link, color("accent"));
    palette.setColor(QPalette::Light, color("borderSoft"));
    palette.setColor(QPalette::Midlight, color("borderSoft"));
    palette.setColor(QPalette::Mid, color("border"));
    palette.setColor(QPalette::Dark, color("track"));
    palette.setColor(QPalette::Shadow, color("title"));
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::HighlightedText})
        palette.setColor(QPalette::Disabled, role, color("disabled"));
    palette.setColor(QPalette::Disabled, QPalette::Highlight, color("track"));
    palette.setColor(QPalette::Disabled, QPalette::Base, color("surface"));
    return palette;
}
}

Mode mode() { return state; }
Mode parse(const QString& text) { return text.compare("dark", Qt::CaseInsensitive) == 0 ? Dark : Light; }
QString name(Mode value) { return value == Dark ? "dark" : "light"; }
QColor color(const QString& token) { return QColor(table().values.value(token, "#ff00ff")); }

void apply(Mode value) {
    state = value;
    if (auto* application = qobject_cast<QApplication*>(QApplication::instance())) {
        application->setPalette(buildPalette());
        application->setStyleSheet(expand(QString::fromUtf8(styleTemplate)));
        for (auto* widget : QApplication::allWidgets()) widget->update();
    }
}
void save(Mode value) {
    const QString path = settingsPath();
    if (path.isEmpty()) return;
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue("appearance/theme", name(value));
}
Mode load() {
    const QString path = settingsPath();
    if (!path.isEmpty()) {
        QSettings settings(path, QSettings::IniFormat);
        const QString saved = settings.value("appearance/theme").toString();
        if (!saved.isEmpty()) return parse(saved);
    }
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark ? Dark : Light;
}
}
