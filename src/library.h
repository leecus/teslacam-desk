#pragma once
#include <QtCore>
#include <QtSql>
#include <atomic>
#include <functional>

namespace Cam {
struct Source { QString id, root, name, lastScan, status; };
struct Clip {
    QString relative, event, kind, camera, stamp;
    qint64 size = 0, modified = 0, duration = 0;
};
struct Event {
    QString key, kind, start, end, thumb, trigger, reason, city;
    qint64 bytes = 0;
    int files = 0, cameras = 0;
    bool favorite = false, exported = false;
};
struct PreviewTarget {
    Clip clip;
    int offsetMs = 0;
};
struct Result {
    bool canceled = false;
    int files = 0, changed = 0, removed = 0, events = 0;
    qint64 bytes = 0;
    QString destination;
    QStringList errors;
};
using Progress = std::function<void(int, int, const QString&)>;
QString dataDirectory();
QString humanBytes(qint64 bytes);
QString kindLabel(const QString& kind);
QString cameraLabel(const QString& camera);
QString reasonLabel(const QString& reason);
PreviewTarget previewTarget(const QList<Clip>& clips, const QString& trigger);
Source identifySource(const QString& path);
QString verifiedPath(const Source& source, const Clip& clip, bool checkFingerprint = true, bool checkDevice = true);
QString thumbnailPath(const Source& source, const Clip& clip, int offsetMs = 0);

class Database {
public:
    explicit Database(const QString& path);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    QSqlDatabase sql;
    void exec(const QString& query);
    QSqlQuery query(const QString& text, const QVariantList& args = {});
    QList<Source> sources();
    void saveSource(const Source& source);
    QList<Event> events(const QString& sourceId, const QString& kind = {},
                       const QString& from = {}, const QString& to = {}, int state = 0,
                       const QString& search = {}, bool oldest = false);
    QList<Clip> clips(const QString& sourceId, const QString& event);
    void setFavorite(const QString& sourceId, const QString& event, bool favorite);
    void setClipDuration(const QString& sourceId, const QString& relative, qint64 duration);
private:
    QString connection;
};

Result scan(const QString& dbPath, const Source& source, bool rebuild,
            std::atomic_bool& cancel, const Progress& progress = {});
Result exportOriginals(const QString& dbPath, const Source& source, const QStringList& keys,
                       const QString& destination, std::atomic_bool& cancel,
                       const Progress& progress = {});
Result deleteEvents(const QString& dbPath, const Source& source, const QStringList& keys,
                    std::atomic_bool& cancel, const Progress& progress = {}, bool removeLeftovers = false);
bool isWithin(const QString& root, const QString& path);
}
