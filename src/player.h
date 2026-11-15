#pragma once
#include "library.h"
#include <QtWidgets>
#include <QtMultimedia>
#include <QtMultimediaWidgets>

class EventPlayer : public QWidget {
    Q_OBJECT
public:
    explicit EventPlayer(QWidget* parent = nullptr);
    void load(const Cam::Source& source, const QList<Cam::Clip>& clips, const QString& trigger = {});
    void clear();
    void pause();
    QString camera() const;
    int rangeStart() const;
    int rangeEnd() const;
    void saveScreenshot();
signals:
    void rangeExportRequested();
    void clipDurationKnown(const QString& relative, qint64 duration);
private:
    struct Lane {
        QString camera, path;
        QMediaPlayer* player = nullptr;
        QVideoWidget* video = nullptr;
        QLabel* status = nullptr;
    };
    Cam::Source source;
    QList<Cam::Clip> clips;
    QList<Lane*> lanes;
    QMap<QString,qint64> durations;
    QDateTime start;
    QWidget* surface;
    QGridLayout* grid;
    QLabel* time;
    QLabel* marker;
    QSlider* slider;
    QWidget* coverage;
    QPushButton* play;
    QComboBox* speed;
    QComboBox* cameraChoice;
    QSpinBox *from, *to;
    QWidget *transport, *tools, *rangePanel;
    QToolButton* cropToggle;
    QTimer timer;
    QElapsedTimer elapsed;
    qint64 position=0, anchor=0, length=0;
    bool playing=false;
    void seek(qint64 value);
    void sync();
    qint64 clipOffset(const Cam::Clip& clip) const;
    void updateLength();
    void refreshCoverage();
    bool eventFilter(QObject* watched,QEvent* event) override;
};
