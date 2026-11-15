#pragma once
#include <QColor>
#include <QString>
#include <QPalette>

// Holds the two colour sets, the widget stylesheet that references them, and the
// persistence of the chosen mode. All colours live here so widgets never hardcode hex.
namespace Theme {
enum Mode { Light, Dark };

Mode mode();
Mode parse(const QString& name);
QString name(Mode mode);
QColor color(const QString& token);

// Applies palette and stylesheet to the running application and remembers the mode.
void apply(Mode mode);
void save(Mode mode);
// Saved mode, otherwise the system colour scheme, otherwise light.
Mode load();
}
