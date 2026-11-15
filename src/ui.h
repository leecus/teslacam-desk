#pragma once
#include <QtWidgets>

namespace Ui {
inline QToolButton* info(const QString& name,const QString& description,QWidget* parent=nullptr) {
    auto button=new QToolButton(parent);
    button->setText("ⓘ"); button->setObjectName("infoButton"); button->setAutoRaise(true);
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
inline void primary(QPushButton* button,bool enabled) {
    const QString name=enabled?"primary":"";
    if(button->objectName()==name) return;
    button->setObjectName(name); button->style()->unpolish(button); button->style()->polish(button);
}
}
