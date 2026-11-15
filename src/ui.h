#pragma once
#include <QtWidgets>
#include "theme.h"

namespace Ui {
inline QToolButton* info(const QString& name,const QString& description,QWidget* parent=nullptr) {
    auto button=new QToolButton(parent);
    button->setText("ⓘ"); button->setObjectName("iconButton"); button->setAutoRaise(true);
    button->setFocusPolicy(Qt::StrongFocus); button->setAccessibleName(name);
    button->setAccessibleDescription(description); button->setToolTip(description);
    QObject::connect(button,&QToolButton::clicked,button,[button,description]{
        QToolTip::showText(button->mapToGlobal(QPoint(0,button->height())),description,button);
    });
    return button;
}
inline QLabel* label(const QString& text,QWidget* buddy) {
    auto result=new QLabel(text); result->setBuddy(buddy); return result;
}
// Crisp chevron for navigation buttons; Qt's stock arrow icons bring their own colors.
inline QIcon chevron(bool left,const QColor& color=Theme::color("accent")) {
    const qreal ratio=2, size=16;
    QPixmap pixmap(int(size*ratio),int(size*ratio)); pixmap.setDevicePixelRatio(ratio); pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap); painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color,1.8); pen.setCapStyle(Qt::RoundCap); pen.setJoinStyle(Qt::RoundJoin); painter.setPen(pen);
    QPainterPath path;
    if(left) {path.moveTo(10,3.5); path.lineTo(5,8); path.lineTo(10,12.5);}
    else {path.moveTo(6,3.5); path.lineTo(11,8); path.lineTo(6,12.5);}
    painter.drawPath(path);
    return QIcon(pixmap);
}
// One calendar look for the whole app: Qt paints the weekday header from the day view's
// AlternateBase, colours weekends red and ships stock arrow icons, none of which match the palette.
inline void styleCalendar(QCalendarWidget* calendar) {
    if(!calendar) return;
    calendar->setAttribute(Qt::WA_StyledBackground,true);
    calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    calendar->setGridVisible(false);
    // Month and weekday names come from the widget locale, which does not follow a
    // later QLocale::setDefault, so it is set explicitly on every restyle.
    calendar->setLocale(QLocale());
    QPalette palette=calendar->palette();
    palette.setColor(QPalette::AlternateBase,Theme::color("surface"));
    palette.setColor(QPalette::Window,Theme::color("surface"));
    calendar->setPalette(palette);
    if(auto* view=calendar->findChild<QAbstractItemView*>("qt_calendar_calendarview")) {
        QPalette viewPalette=view->palette();
        viewPalette.setColor(QPalette::Base,Theme::color("surface"));
        viewPalette.setColor(QPalette::AlternateBase,Theme::color("surface"));
        viewPalette.setColor(QPalette::Disabled,QPalette::Text,Theme::color("disabled"));
        view->setPalette(viewPalette);
    }
    QTextCharFormat dayFormat; dayFormat.setForeground(Theme::color("text"));
    for(const auto day:{Qt::Monday,Qt::Tuesday,Qt::Wednesday,Qt::Thursday,Qt::Friday,Qt::Saturday,Qt::Sunday})
        calendar->setWeekdayTextFormat(day,dayFormat);
    if(auto* button=calendar->findChild<QToolButton*>("qt_calendar_prevmonth")) {button->setIcon(chevron(true)); button->setIconSize(QSize(16,16)); button->setAccessibleName("上一月");}
    if(auto* button=calendar->findChild<QToolButton*>("qt_calendar_nextmonth")) {button->setIcon(chevron(false)); button->setIconSize(QSize(16,16)); button->setAccessibleName("下一月");}
}
inline void primary(QPushButton* button,bool enabled) {
    const QString name=enabled?"primary":"";
    if(button->objectName()==name) return;
    button->setObjectName(name); button->style()->unpolish(button); button->style()->polish(button);
}
}
