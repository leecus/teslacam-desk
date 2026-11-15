#pragma once
#include "library.h"
#include "player.h"
#include <QtWidgets>
#include <memory>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString& initialSource = {});
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    QString dbPath;
    std::unique_ptr<Cam::Database> db;
    QList<Cam::Source> knownSources;
    QList<Cam::Event> eventList;
    QComboBox *sources, *kind, *state, *order;
    QCalendarWidget* calendar;
    QDateTimeEdit *from, *to;
    QCheckBox* timeEnabled;
    QLineEdit* search;
    QTableWidget* table;
    EventPlayer* player;
    QLabel *capacity, *scanInfo, *summary, *eventInfo, *selectionInfo, *status, *pageInfo;
    QProgressBar *diskBar, *progress;
    QPushButton *scanButton, *chooseButton, *cancelButton, *favoriteButton, *originalButton, *deleteButton, *backupButton, *revealButton;
    QAction *fullScanAction, *thumbnailAction;
    QLabel* listEmpty;
    QStackedWidget* eventStack;
    QWidget* listTools;
    QPushButton* allDates;
    QPushButton *previous, *next;
    QList<QWidget*> operationControls;
    QSet<QString> checked;
    Cam::Source current;
    QString currentEvent;
    std::atomic_bool canceled{false};
    QThread* worker=nullptr;
    bool closing=false, filling=false;
    bool allowBrowse=true, thumbnailJob=false;
    int page=0;
    QTimer refreshTimer;
    QElapsedTimer jobElapsed;
    QString ffmpeg() const;
    void buildUi();
    void loadSources(const QString& preferred = {});
    void chooseSource();
    void refresh();
    void fillTable();
    void loadEvent(int row);
    void updateSelection();
    void setBusy(bool value);
    void startScan(bool rebuild);
    void exportOriginals();
    void clean(bool backupFirst);
    void exportRange();
    void createThumbnails();
    void report(const QString& title, const Cam::Result& result, const QString& error);
    void run(const QString& title, std::function<Cam::Result(std::atomic_bool&,const Cam::Progress&)> work, bool thumbsAfter = false);
    QStringList selectedKeys() const;
};
