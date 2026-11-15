#pragma once
#include "library.h"
#include "player.h"
#include "theme.h"
#include "i18n.h"
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
    QToolButton* themeButton;
    QMenu* languageMenu;
    QList<QWidget*> operationControls;
    QSet<QString> checked;
    Cam::Source current;
    QString currentEvent;
    QString autoThumbKey;
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
    void scheduleThumbnails();
    void applyTheme(Theme::Mode mode, bool remember);
    void updateThemeButton();
    void loadEvent(int row);
    void updateSelection();
    void setBusy(bool value);
    void startScan(bool rebuild);
    void exportOriginals();
    void clean(bool backupFirst);
    void exportRange();
    void createThumbnails();
    void report(const QString& title, const Cam::Result& result, const QString& error);
    // Job behaviour used to be derived from the title text; it must survive translation.
    struct JobSpec {
        QString title;
        bool thumbsAfter = false;
        bool browseDuring = true;
        bool thumbnailJob = false;
        bool refreshDuring = false;
    };
    void run(const JobSpec& spec, std::function<Cam::Result(std::atomic_bool&,const Cam::Progress&)> work);
    void switchLanguage(I18n::Language language);
    void rebuildForLanguage(I18n::Language language);
    QStringList selectedKeys() const;
};
