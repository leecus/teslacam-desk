#include "library.h"
#include <QCoreApplication>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <stdexcept>
#include <filesystem>

namespace Cam {
static void fail(const QString& message) { throw std::runtime_error(message.toUtf8().constData()); }
QString dataDirectory() {
    QString path = qEnvironmentVariable("TESLACAM_DATA_DIR");
    if (path.isEmpty()) path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!QDir().mkpath(path)) fail(QCoreApplication::translate("Cam","无法创建本地索引目录：%1").arg(path));
    return QDir(path).absolutePath();
}
QString settingsFile() { return dataDirectory() + "/settings.ini"; }
QString humanBytes(qint64 bytes) {
    const QStringList units{"B", "KB", "MB", "GB", "TB"};
    double value = double(bytes); int unit = 0;
    while (value >= 1024 && unit < 4) { value /= 1024; ++unit; }
    return QString::number(value, 'f', unit < 2 ? 0 : 1) + " " + units[unit];
}
QString kindLabel(const QString& kind) {
    if (kind == "sentry") return QCoreApplication::translate("Cam","哨兵录像");
    if (kind == "saved") return QCoreApplication::translate("Cam","手动保存");
    if (kind == "recent") return QCoreApplication::translate("Cam","近期行车 · 推断分组");
    return QCoreApplication::translate("Cam","其他录像");
}
QString cameraLabel(const QString& camera) {
    if (camera == "front") return QCoreApplication::translate("Cam","前方");
    if (camera == "back") return QCoreApplication::translate("Cam","后方");
    if (camera == "left_repeater") return QCoreApplication::translate("Cam","左侧");
    if (camera == "right_repeater") return QCoreApplication::translate("Cam","右侧");
    if (camera == "left_pillar") return QCoreApplication::translate("Cam","左前");
    if (camera == "right_pillar") return QCoreApplication::translate("Cam","右前");
    return camera;
}
static const QStringList& reasonKeys() {
    static const QStringList keys{"sentry_aware_object_detection", "user_interaction_honk", "user_interaction_dashcam"};
    return keys;
}
QString reasonLabel(const QString& reason) {
    if (reason.isEmpty()) return {};
    if (reason == "sentry_aware_object_detection") return QCoreApplication::translate("Cam","检测到物体");
    if (reason == "user_interaction_honk") return QCoreApplication::translate("Cam","鸣笛");
    if (reason == "user_interaction_dashcam") return QCoreApplication::translate("Cam","手动保存");
    return reason;
}
static QDateTime localTime(const QString& text) {
    QDateTime plain = QDateTime::fromString(text, "yyyy-MM-dd HH:mm:ss");
    if (plain.isValid()) return plain;
    QDateTime iso = QDateTime::fromString(text, Qt::ISODate);
    if (iso.isValid()) return iso.timeSpec()==Qt::LocalTime ? iso : iso.toLocalTime();
    QDateTime dashed = QDateTime::fromString(text, "yyyy-MM-ddTHH-mm-ss");
    return dashed.isValid() ? dashed : QDateTime();
}
static int cameraRank(const QString& camera) {
    static const QStringList order{"front", "back", "left_repeater", "right_repeater", "left_pillar", "right_pillar"};
    const int index = order.indexOf(camera);
    return index < 0 ? order.size() : index;
}
PreviewTarget previewTarget(const QList<Clip>& clips, const QString& trigger) {
    PreviewTarget best;
    const QDateTime moment = localTime(trigger);
    auto prefer = [](const Clip& candidate, const Clip& current) {
        if (current.relative.isEmpty()) return true;
        const int rank = cameraRank(candidate.camera) - cameraRank(current.camera);
        if (rank != 0) return rank < 0;
        return candidate.stamp < current.stamp;
    };
    auto covers = [&](const Clip& clip, int* offset) {
        const QDateTime stamp = localTime(clip.stamp);
        if (!moment.isValid() || !stamp.isValid()) return false;
        const qint64 delta = stamp.msecsTo(moment);
        const qint64 span = clip.duration > 0 ? clip.duration : 60000;
        if (delta < 0 || delta >= span) return false;
        if (offset) *offset = int(delta);
        return true;
    };
    for (const auto& clip : clips) if (covers(clip, nullptr) && prefer(clip, best.clip)) best.clip = clip;
    if (best.clip.relative.isEmpty()) for (const auto& clip : clips) if (prefer(clip, best.clip)) best.clip = clip;
    int offset = 0;
    if (!best.clip.relative.isEmpty()) {
        if(covers(best.clip,&offset)) best.offsetMs=offset;
        else best.offsetMs=int(best.clip.duration>0?qMin<qint64>(1000,best.clip.duration/2):1000);
    }
    return best;
}
bool isWithin(const QString& root, const QString& path) {
    QString r = QDir::cleanPath(QDir::fromNativeSeparators(root));
    QString p = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    r = r.toCaseFolded(); p = p.toCaseFolded();
#endif
    if (!r.endsWith('/')) r += '/';
    return p.startsWith(r);
}
static bool linked(const QFileInfo& info) {
    return info.isSymLink() || info.isJunction();
}
Source identifySource(const QString& path) {
    QFileInfo chosen(path);
    if (!chosen.exists() || !chosen.isDir() || linked(chosen)) fail(QCoreApplication::translate("Cam","录像目录不存在，或目录是链接。"));
    QString root = chosen.canonicalFilePath();
    if (QDir(root).exists("TeslaCam")) root = QFileInfo(QDir(root).filePath("TeslaCam")).canonicalFilePath();
    if (root.isEmpty() || linked(QFileInfo(QDir(chosen.absoluteFilePath()).filePath("TeslaCam"))))
        fail(QCoreApplication::translate("Cam","不能使用链接形式的 TeslaCam 目录。"));
    // Reject links in all ancestors; actions must never traverse junctions to another target.
    QString ancestor = root;
    while (true) {
        if (linked(QFileInfo(ancestor))) fail(QCoreApplication::translate("Cam","录像目录路径含符号链接或目录联接。"));
        const QString parent = QFileInfo(ancestor).dir().absolutePath();
        if (parent == ancestor) break;
        ancestor = parent;
    }
    QString device;
    QStorageInfo storage(root);
#ifdef Q_OS_WIN
    wchar_t volumeRoot[MAX_PATH] = {}, guid[MAX_PATH] = {};
    DWORD serial = 0;
    if (!GetVolumePathNameW(reinterpret_cast<LPCWSTR>(root.utf16()), volumeRoot, MAX_PATH) ||
        !GetVolumeInformationW(volumeRoot, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0))
        fail(QCoreApplication::translate("Cam","无法读取磁盘身份，请重新连接设备。"));
    if (GetVolumeNameForVolumeMountPointW(volumeRoot, guid, MAX_PATH)) device = QString::fromWCharArray(guid);
    device += ":" + QString::number(serial, 16);
#else
    device = QString::fromUtf8(storage.device());
#endif
    const QString relative = QDir(storage.rootPath()).relativeFilePath(root).toCaseFolded();
    Source source;
    source.id = QString::fromLatin1(QCryptographicHash::hash((device + "/" + relative).toUtf8(), QCryptographicHash::Sha256).toHex());
    source.root = root;
    source.name = (storage.name().isEmpty() ? QCoreApplication::translate("Cam","录像设备") : storage.name()) + " · " + QDir::toNativeSeparators(root);
    source.status = QCoreApplication::translate("Cam","尚未扫描");
    return source;
}
QString verifiedPath(const Source& source, const Clip& clip, bool checkFingerprint, bool checkDevice) {
    if (checkDevice && identifySource(source.root).id != source.id) fail(QCoreApplication::translate("Cam","磁盘身份已变化，已停止操作。"));
    const QString rel = QDir::fromNativeSeparators(clip.relative);
    if (rel.isEmpty() || QDir::isAbsolutePath(rel) || rel.split('/').contains("..")) fail(QCoreApplication::translate("Cam","索引文件路径无效。"));
    const QString full = QDir(source.root).absoluteFilePath(rel);
    QString parent = full;
    while (isWithin(source.root, parent)) {
        if (linked(QFileInfo(parent))) fail(QCoreApplication::translate("Cam","文件路径已变为链接：%1").arg(rel));
        parent = QFileInfo(parent).dir().absolutePath();
    }
    QFileInfo info(full);
    if (!info.exists() || !info.isFile() || !isWithin(source.root, info.canonicalFilePath())) fail(QCoreApplication::translate("Cam","文件已离线或不存在：%1").arg(rel));
    if (checkFingerprint && (info.size() != clip.size || info.lastModified().toMSecsSinceEpoch() != clip.modified))
        fail(QCoreApplication::translate("Cam","文件已变化，请更新索引后重试：%1").arg(rel));
    return info.canonicalFilePath();
}
QString thumbnailPath(const Source& source, const Clip& clip, int offsetMs) {
    QString identity = source.id + "/" + clip.relative + "/" + QString::number(clip.size) + "/" + QString::number(clip.modified);
    if (offsetMs > 0) identity += "/" + QString::number(offsetMs);
    const QByteArray value = identity.toUtf8();
    const QString dir = dataDirectory() + "/thumbnails";
    QDir().mkpath(dir);
    return dir + "/" + QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex()) + ".jpg";
}
Database::Database(const QString& path) {
    connection = QUuid::createUuid().toString(QUuid::WithoutBraces);
    sql = QSqlDatabase::addDatabase("QSQLITE", connection);
    sql.setDatabaseName(path);
    sql.setConnectOptions("QSQLITE_BUSY_TIMEOUT=10000");
    try {
    if (!sql.open()) fail(QCoreApplication::translate("Cam","无法打开索引：%1").arg(sql.lastError().text()));
    // Qt 6.8.3 bundles SQLite 3.49.1, affected by the WAL-reset corruption bug.
    // Use rollback journals for this local cache; see https://sqlite.org/wal.html .
    exec("PRAGMA journal_mode=DELETE");
    exec("PRAGMA synchronous=FULL");
    exec("PRAGMA foreign_keys=ON");
    exec("CREATE TABLE IF NOT EXISTS sources(id TEXT PRIMARY KEY,root TEXT,name TEXT,last_scan TEXT,status TEXT)");
    exec("CREATE TABLE IF NOT EXISTS clips(source TEXT,rel TEXT,event TEXT,kind TEXT,stamp TEXT,camera TEXT,bytes INTEGER,modified INTEGER,seen TEXT,duration INTEGER,PRIMARY KEY(source,rel))");
    exec("CREATE INDEX IF NOT EXISTS clips_event ON clips(source,event,stamp)");
    exec("CREATE INDEX IF NOT EXISTS clips_time ON clips(source,stamp,kind)");
    exec("CREATE TABLE IF NOT EXISTS flags(source TEXT,event TEXT,favorite INTEGER DEFAULT 0,exported INTEGER DEFAULT 0,PRIMARY KEY(source,event))");
    exec("CREATE TABLE IF NOT EXISTS event_meta(source TEXT,event TEXT,trigger TEXT,reason TEXT,city TEXT,PRIMARY KEY(source,event))");
    bool hasDuration = false;
    QSqlQuery columns(sql);
    if (!columns.exec("PRAGMA table_info(clips)")) fail(columns.lastError().text());
    while (columns.next()) if (columns.value(1).toString() == "duration") hasDuration = true;
    if (!hasDuration) exec("ALTER TABLE clips ADD COLUMN duration INTEGER");
    } catch(...) {
        sql.close(); sql=QSqlDatabase(); QSqlDatabase::removeDatabase(connection); throw;
    }
}
Database::~Database() { sql.close(); sql = QSqlDatabase(); QSqlDatabase::removeDatabase(connection); }
void Database::exec(const QString& text) { QSqlQuery q(sql); if (!q.exec(text)) fail(q.lastError().text()); }
QSqlQuery Database::query(const QString& text, const QVariantList& args) {
    QSqlQuery q(sql);
    if (!q.prepare(text)) fail(q.lastError().text());
    for (const auto& a : args) q.addBindValue(a);
    if (!q.exec()) fail(q.lastError().text());
    return q;
}
QList<Source> Database::sources() {
    auto q = query("SELECT id,root,name,last_scan,status FROM sources ORDER BY name"); QList<Source> result;
    while (q.next()) result.append({q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString(), q.value(4).toString()});
    if(q.lastError().isValid()) fail(q.lastError().text());
    return result;
}
void Database::saveSource(const Source& s) {
    query("INSERT INTO sources VALUES(?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET root=excluded.root,name=excluded.name,last_scan=excluded.last_scan,status=excluded.status",
          {s.id, s.root, s.name, s.lastScan, s.status});
}
QList<Event> Database::events(const QString& id, const QString& kind, const QString& from, const QString& to, int state, const QString& search, bool oldest) {
    QString text = "SELECT c.event,c.kind,MIN(c.stamp),MAX(c.stamp),SUM(c.bytes),COUNT(*),COUNT(DISTINCT c.camera),COALESCE(f.favorite,0),COALESCE(f.exported,0),COALESCE(MAX(m.trigger),''),COALESCE(MAX(m.reason),''),COALESCE(MAX(m.city),'') FROM clips c LEFT JOIN flags f ON f.source=c.source AND f.event=c.event LEFT JOIN event_meta m ON m.source=c.source AND m.event=c.event WHERE c.source=?";
    QVariantList args{id};
    if (!kind.isEmpty()) { text += " AND c.kind=?"; args << kind; }
    if (state == 1) text += " AND f.favorite=1";
    if (state == 2) text += " AND f.exported=1";
    if (state == 3) text += " AND COALESCE(f.exported,0)=0";
    text += " GROUP BY c.event HAVING 1=1";
    // Overlap uses a stored duration. Unknown clips still count as about one minute.
    if (!from.isEmpty()) { text += " AND MAX(datetime(c.stamp, '+' || (COALESCE(NULLIF(c.duration,0),60000)/1000.0) || ' seconds'))>?"; args << from; }
    if (!to.isEmpty()) { text += " AND MIN(c.stamp)<=?"; args << to; }
    if (!search.isEmpty()) {
        text += " AND (instr(lower(c.event),lower(?))>0 OR instr(MIN(c.stamp),?)>0 OR instr(lower(COALESCE(MAX(m.reason),'')),lower(?))>0 OR instr(lower(COALESCE(MAX(m.city),'')),lower(?))>0 OR instr(lower(COALESCE(MAX(m.trigger),'')),lower(?))>0";
        args << search << search << search << search << search;
        for (const auto& key : reasonKeys()) {
            if (!reasonLabel(key).contains(search, Qt::CaseInsensitive)) continue;
            text += " OR MAX(m.reason)=?"; args << key;
        }
        text += ")";
    }
    text += oldest ? " ORDER BY MIN(c.stamp) ASC" : " ORDER BY MIN(c.stamp) DESC";
    auto q = query(text, args); QList<Event> result;
    while (q.next()) {
        Event e; e.key=q.value(0).toString(); e.kind=q.value(1).toString(); e.start=q.value(2).toString(); e.end=q.value(3).toString();
        e.bytes=q.value(4).toLongLong(); e.files=q.value(5).toInt(); e.cameras=q.value(6).toInt(); e.favorite=q.value(7).toBool(); e.exported=q.value(8).toBool();
        e.trigger=q.value(9).toString(); e.reason=q.value(10).toString(); e.city=q.value(11).toString(); result << e;
    }
    if(q.lastError().isValid()) fail(q.lastError().text());
    return result;
}
QList<Clip> Database::clips(const QString& id, const QString& event) {
    auto q = query("SELECT rel,event,kind,camera,stamp,bytes,modified,COALESCE(duration,0) FROM clips WHERE source=? AND event=? ORDER BY stamp,camera", {id,event});
    QList<Clip> result;
    while(q.next()) result.append({q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),q.value(4).toString(),q.value(5).toLongLong(),q.value(6).toLongLong(),q.value(7).toLongLong()});
    if(q.lastError().isValid()) fail(q.lastError().text());
    return result;
}
void Database::setFavorite(const QString& id, const QString& event, bool value) {
    query("INSERT INTO flags(source,event,favorite) VALUES(?,?,?) ON CONFLICT(source,event) DO UPDATE SET favorite=excluded.favorite", {id,event,value});
}
void Database::setClipDuration(const QString& id, const QString& relative, qint64 duration) {
    if (duration <= 0) return;
    query("UPDATE clips SET duration=? WHERE source=? AND rel=?", {duration, id, relative});
}
IndexRecovery recoverIndex(const QString& path) {
    IndexRecovery result;
    if(!QFileInfo::exists(path)) return result;
    // Inspect a private snapshot so SQLite cannot retain a WAL/file handle on the
    // live path while it is being archived and replaced on Windows.
    QTemporaryDir inspection;
    if(!inspection.isValid()) fail(QCoreApplication::translate("Cam","无法创建索引检查目录"));
    const QString checkPath=inspection.filePath("index.sqlite");
    for(const QString& suffix:QStringList{"","-wal","-journal"}) {
        const QString original=path+suffix;
        if(!QFileInfo::exists(original)) continue;
        const QFileInfo before(original);
        const qint64 bytes=before.size(),modified=before.lastModified().toMSecsSinceEpoch();
        if(!QFile::copy(original,checkPath+suffix)) fail(QCoreApplication::translate("Cam","无法读取索引快照，请关闭其他实例后重试"));
        const QFileInfo after(original);
        if(after.size()!=bytes || after.lastModified().toMSecsSinceEpoch()!=modified)
            fail(QCoreApplication::translate("Cam","索引正在被其他进程修改，请稍后重试"));
    }
    const QString connection="index-check-"+QUuid::createUuid().toString(QUuid::Id128);
    auto check=QSqlDatabase::addDatabase("QSQLITE",connection);
    check.setDatabaseName(checkPath);
    // The private copy must be writable so SQLite can roll back a hot journal
    // before integrity checks. The original files remain untouched here.
    check.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
    QList<Source> sources;
    struct Flag {QString source,event; bool favorite;};
    QList<Flag> flags;
    QString diagnostic;
    bool corrupted=false, flagsComplete=true;
    auto corruption=[](const QSqlError& error) {
        const int code=error.nativeErrorCode().toInt() & 0xff;
        return code==11 || code==26;
    };
    try {
        if(!check.open()) {
            if(!corruption(check.lastError())) fail(check.lastError().text());
            corrupted=true; diagnostic=check.lastError().text();
        } else {
            QSqlQuery q(check);
            if(!q.exec("PRAGMA quick_check")) {
                if(!corruption(q.lastError())) fail(q.lastError().text());
                corrupted=true; diagnostic=q.lastError().text();
            } else {
                while(q.next()) if(q.value(0).toString()!="ok") {
                    corrupted=true;
                    if(diagnostic.size()<4096) diagnostic+=q.value(0).toString().left(4096)+"\n";
                }
                if(q.lastError().isValid()) {
                    if(!corruption(q.lastError())) fail(q.lastError().text());
                    corrupted=true; diagnostic+=q.lastError().text();
                }
            }
            if(corrupted) {
                // Table scans can salvage preferences even when a secondary index is damaged.
                QSqlQuery s(check);
                if(s.exec("SELECT id,root,name FROM sources NOT INDEXED"))
                    while(s.next()) {
                        Source source{s.value(0).toString(),s.value(1).toString(),s.value(2).toString(),{},
                                      QCoreApplication::translate("Cam","索引已恢复，请重新扫描")};
                        if(!source.id.isEmpty() && !source.root.isEmpty()) sources << source;
                    }
                QSqlQuery f(check);
                if(f.exec("SELECT source,event,favorite FROM flags NOT INDEXED")) {
                    while(f.next()) if(f.value(2).toBool()) flags.append({f.value(0).toString(),f.value(1).toString(),true});
                    flagsComplete=!f.lastError().isValid();
                } else flagsComplete=false;
            }
        }
    } catch(...) {
        check.close(); check=QSqlDatabase(); QSqlDatabase::removeDatabase(connection); throw;
    }
    check.close(); check=QSqlDatabase(); QSqlDatabase::removeDatabase(connection);
    if(!corrupted) return result;
    const QString identifier=QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+"-"+QUuid::createUuid().toString(QUuid::Id128).left(8);
    const QString backup=QFileInfo(path).absolutePath()+"/recovery/"+identifier;
    if(!QDir().mkpath(backup)) fail(QCoreApplication::translate("Cam","无法备份损坏索引：%1").arg(backup));
    const QString replacement=backup+"/rebuilt.sqlite";
    {
        Database fresh(replacement);
        if(!fresh.sql.transaction()) fail(fresh.sql.lastError().text());
        for(const auto& source:sources) fresh.saveSource(source);
        for(const auto& flag:flags) fresh.setFavorite(flag.source,flag.event,flag.favorite);
        if(!fresh.sql.commit()) fail(fresh.sql.lastError().text());
        auto q=fresh.query("PRAGMA quick_check");
        if(!q.next() || q.value(0).toString()!="ok") fail(QCoreApplication::translate("Cam","新索引校验失败"));
    }
    QStringList moved;
    auto restore=[&] {
        for(auto it=moved.crbegin();it!=moved.crend();++it)
            QFile::rename(QDir(backup).filePath(QFileInfo(path+*it).fileName()),path+*it);
    };
    for(const QString& suffix:QStringList{"","-wal","-shm","-journal"}) {
        const QString original=path+suffix;
        if(!QFileInfo::exists(original)) continue;
        if(!QFile::rename(original,QDir(backup).filePath(QFileInfo(original).fileName()))) {
            restore(); fail(QCoreApplication::translate("Cam","索引正在使用或无法备份，请关闭其他应用后重试"));
        }
        moved << suffix;
    }
    QFile rebuilt(replacement);
    if(!rebuilt.rename(path)) {const QString error=rebuilt.errorString(); restore(); fail(QCoreApplication::translate("Cam","无法替换损坏索引，原文件已保留：%1").arg(error));}
    QFile note(backup+"/recovery.json");
    if(note.open(QIODevice::WriteOnly)) note.write(QJsonDocument(QJsonObject{
        {"diagnostic",diagnostic.left(8192)},{"restored_sources",int(sources.size())},
        {"restored_favorites",int(flags.size())},{"favorites_complete",flagsComplete}}).toJson());
    result.backupDirectory=backup; result.sources=int(sources.size()); result.favorites=int(flags.size());
    return result;
}
static void storeEventMeta(Database& db, const QString& sourceId, const QString& event, const QString& videoPath) {
    const QFileInfo info(QDir(QFileInfo(videoPath).absolutePath()).filePath("event.json"));
    if (!info.exists()) { db.query("DELETE FROM event_meta WHERE source=? AND event=?", {sourceId, event}); return; }
    if (!info.isFile() || info.isSymLink() || info.size() <= 0 || info.size() >= 1024 * 1024) return;
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) return;
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    const QString trigger = object.value("timestamp").toString(), reason = object.value("reason").toString(), city = object.value("city").toString();
    if (trigger.isEmpty() && reason.isEmpty() && city.isEmpty()) db.query("DELETE FROM event_meta WHERE source=? AND event=?", {sourceId, event});
    else db.query("INSERT INTO event_meta(source,event,trigger,reason,city) VALUES(?,?,?,?,?) ON CONFLICT(source,event) DO UPDATE SET trigger=excluded.trigger,reason=excluded.reason,city=excluded.city", {sourceId, event, trigger, reason, city});
}
static Clip describe(const QString& root, const QFileInfo& info) {
    Clip c; c.relative=QDir(root).relativeFilePath(info.absoluteFilePath()); c.size=info.size(); c.modified=info.lastModified().toMSecsSinceEpoch();
    const auto parts = c.relative.split('/');
    if (parts.contains("SentryClips",Qt::CaseInsensitive)) c.kind="sentry";
    else if (parts.contains("SavedClips",Qt::CaseInsensitive)) c.kind="saved";
    else if (parts.contains("RecentClips",Qt::CaseInsensitive)) c.kind="recent";
    else c.kind="other";
    static const QRegularExpression pattern("^(\\d{4}-\\d{2}-\\d{2}_\\d{2}-\\d{2}-\\d{2})-(.+)\\.mp4$",QRegularExpression::CaseInsensitiveOption);
    auto match=pattern.match(info.fileName());
    QDateTime timestamp;
    if (match.hasMatch()) { timestamp=QDateTime::fromString(match.captured(1),"yyyy-MM-dd_HH-mm-ss"); c.camera=match.captured(2).toLower(); }
    if (!timestamp.isValid()) timestamp=info.lastModified();
    if (c.camera.isEmpty()) c.camera="unknown";
    c.stamp=timestamp.toString("yyyy-MM-dd HH:mm:ss");
    if (c.kind=="recent") {
        QTime t=timestamp.time(); timestamp.setTime(QTime(t.hour(),t.minute()/10*10));
        c.event=QFileInfo(c.relative).path()+"/"+timestamp.toString("yyyy-MM-dd_HH-mm");
    } else c.event=QFileInfo(c.relative).path();
    return c;
}
static void enumerate(const QString& dir, QStringList& files, std::atomic_bool& cancel, const Progress& progress, int& visited) {
    if (cancel) return;
    QFileInfo info(dir);
    if (!info.isReadable() || linked(info)) fail(QCoreApplication::translate("Cam","目录不可读取或含链接：%1").arg(dir));
    std::error_code ec;
    const std::filesystem::path native(dir.toStdWString());
    std::filesystem::directory_iterator iterator(native,ec),end;
    if(ec) fail(QCoreApplication::translate("Cam","无法枚举目录（不会移除旧索引）：%1 · %2").arg(dir,QString::fromStdString(ec.message())));
    while(iterator!=end) {
        if (cancel) break;
        const QFileInfo entry(QString::fromStdWString(iterator->path().wstring()));
        if (!linked(entry)) {
            if (entry.isDir()) enumerate(entry.absoluteFilePath(),files,cancel,progress,visited);
            else if(entry.suffix().compare("mp4",Qt::CaseInsensitive)==0) {
                files << entry.absoluteFilePath();
                if (++visited%100==0 && progress) progress(visited,0,QCoreApplication::translate("Cam","发现视频文件，正在枚举目录…"));
            }
        }
        iterator.increment(ec);
        if(ec) fail(QCoreApplication::translate("Cam","目录读取中断（不会移除旧索引）：%1").arg(dir));
    }
}
Result scan(const QString& dbPath, const Source& source, bool rebuild, std::atomic_bool& cancel, const Progress& progress) {
    Result result; Database db(dbPath); Source current=source;
    for(const auto& old:db.sources()) if(old.id==source.id) current.lastScan=old.lastScan;
    if(identifySource(source.root).id!=source.id) fail(QCoreApplication::translate("Cam","设备身份不匹配。"));
    current.status=QCoreApplication::translate("Cam","扫描中（列表可能不完整）"); db.saveSource(current);
    QStringList paths; int visited=0;
    try {
        enumerate(source.root,paths,cancel,progress,visited);
        paths.sort(Qt::CaseInsensitive);
        const QString generation=QUuid::createUuid().toString();
        QHash<QString,QPair<qint64,qint64>> known;
        {
            auto q=db.query("SELECT rel,bytes,modified FROM clips WHERE source=?",{source.id});
            while(q.next()) known.insert(q.value(0).toString(),{q.value(1).toLongLong(),q.value(2).toLongLong()});
            if(q.lastError().isValid()) fail(q.lastError().text());
        }
        bool transaction=false; QSet<QString> metaEvents;
        try {
            for(int i=0;i<paths.size() && !cancel;++i) {
                if(!transaction) { if(!db.sql.transaction()) fail(db.sql.lastError().text()); transaction=true; }
                QFileInfo info(paths[i]); Clip c; QString fileError;
                // Keep old rows for entries which still exist in the directory but cannot be read.
                try {
                    if(!info.exists() || !info.isFile()) fail(QCoreApplication::translate("Cam","无法读取文件信息：%1").arg(paths[i]));
                    c=describe(source.root,info);
                    verifiedPath(source,c,true,false);
                    if(c.size<=0) fail(QCoreApplication::translate("Cam","录像文件为空"));
                    if(!info.isReadable()) fail(QCoreApplication::translate("Cam","录像文件不可读取"));
                } catch(const std::exception& e) {fileError=QString::fromUtf8(e.what());}
                if(!fileError.isEmpty()) {
                    const QString relative=QDir(source.root).relativeFilePath(paths[i]);
                    db.query("UPDATE clips SET seen=? WHERE source=? AND rel=?",{generation,source.id,relative});
                    ++result.skipped;
                    result.errors << QCoreApplication::translate("Cam","跳过无法读取的录像：%1 · %2").arg(relative,fileError);
                    if((i+1)%100==0 || i==paths.size()-1) {
                        if(identifySource(source.root).id!=source.id) fail(QCoreApplication::translate("Cam","设备已断开或被更换。"));
                        if(!db.sql.commit()) fail(db.sql.lastError().text()); transaction=false;
                        if(progress) progress(i+1,int(paths.size()),QCoreApplication::translate("Cam","建立索引 · %1").arg(relative));
                    }
                    continue;
                }
                if(!metaEvents.contains(c.event)) { metaEvents.insert(c.event); storeEventMeta(db,source.id,c.event,paths[i]); }
                const auto previous=known.constFind(c.relative);
                const bool changed=previous==known.cend() || previous->first!=c.size || previous->second!=c.modified;
                if(!changed && !rebuild)
                    db.query("UPDATE clips SET seen=? WHERE source=? AND rel=?",{generation,source.id,c.relative});
                else {
                    db.query("INSERT INTO clips(source,rel,event,kind,stamp,camera,bytes,modified,seen) VALUES(?,?,?,?,?,?,?,?,?) ON CONFLICT(source,rel) DO UPDATE SET event=excluded.event,kind=excluded.kind,stamp=excluded.stamp,camera=excluded.camera,bytes=excluded.bytes,modified=excluded.modified,seen=excluded.seen,duration=CASE WHEN clips.bytes!=excluded.bytes OR clips.modified!=excluded.modified THEN NULL ELSE clips.duration END",
                        {source.id,c.relative,c.event,c.kind,c.stamp,c.camera,c.size,c.modified,generation});
                    if(changed) {
                        db.query("UPDATE flags SET exported=0 WHERE source=? AND event=?",{source.id,c.event});
                        ++result.changed;
                    }
                }
                ++result.files; result.bytes+=c.size;
                if((i+1)%100==0 || i==paths.size()-1) {
                    if(identifySource(source.root).id!=source.id) fail(QCoreApplication::translate("Cam","设备已断开或被更换。"));
                    if(!db.sql.commit()) fail(db.sql.lastError().text()); transaction=false;
                    if(progress) progress(i+1,int(paths.size()),QCoreApplication::translate("Cam","建立索引 · %1").arg(c.relative));
                }
            }
            if(transaction) {
                if(identifySource(source.root).id!=source.id) fail(QCoreApplication::translate("Cam","设备已断开或被更换。"));
                if(!db.sql.commit()) fail(db.sql.lastError().text());
            }
        } catch(...) { if(transaction) db.sql.rollback(); throw; }
        result.canceled=cancel;
        if(!cancel) {
            if(identifySource(source.root).id!=source.id) fail(QCoreApplication::translate("Cam","设备已断开或被更换。"));
            auto q=db.query("DELETE FROM clips WHERE source=? AND seen<>?",{source.id,generation}); result.removed=int(q.numRowsAffected());
            db.query("DELETE FROM event_meta WHERE source=? AND event NOT IN (SELECT DISTINCT event FROM clips WHERE source=?)",{source.id,source.id});
            current.lastScan=QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
            current.status=result.skipped?QCoreApplication::translate("Cam","索引已完成 · %1 个文件无法读取").arg(result.skipped):QCoreApplication::translate("Cam","索引已完成");
        } else current.status=QCoreApplication::translate("Cam","扫描已中断 · 可继续更新");
        db.saveSource(current);
    } catch(...) { current.status=QCoreApplication::translate("Cam","扫描失败 · 保留已有索引"); db.saveSource(current); throw; }
    result.events=int(db.events(source.id).size());
    return result;
}

static QByteArray hashFile(const QString& path, std::atomic_bool& cancel) {
    QFile file(path); if(!file.open(QIODevice::ReadOnly)) fail(QCoreApplication::translate("Cam","无法读取：%1").arg(path));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while(!file.atEnd()) {
        if(cancel) return {};
        const QByteArray data=file.read(1024*1024);
        if(data.isEmpty() && file.error()!=QFileDevice::NoError) fail(QCoreApplication::translate("Cam","读取失败：%1").arg(path));
        hash.addData(data);
    }
    return hash.result();
}
static QString copyVerified(const QString& path, const QString& target, std::atomic_bool& cancel) {
    QFile input(path); if(!input.open(QIODevice::ReadOnly)) fail(QCoreApplication::translate("Cam","无法读取原文件：%1").arg(path));
    if(QFileInfo::exists(target)) fail(QCoreApplication::translate("Cam","目标已存在，禁止覆盖：%1").arg(target));
    if(!QDir().mkpath(QFileInfo(target).absolutePath())) fail(QCoreApplication::translate("Cam","无法创建导出目录。"));
    QSaveFile output(target); if(!output.open(QIODevice::WriteOnly)) fail(QCoreApplication::translate("Cam","无法写入：%1").arg(target));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while(!input.atEnd()) {
        if(cancel) { output.cancelWriting(); return {}; }
        QByteArray block=input.read(1024*1024);
        if(block.isEmpty() && input.error()!=QFileDevice::NoError) fail(QCoreApplication::translate("Cam","原文件读取失败：%1").arg(path));
        hash.addData(block);
        if(output.write(block)!=block.size()) fail(QCoreApplication::translate("Cam","导出失败，可能空间不足：%1").arg(target));
    }
    if(cancel) { output.cancelWriting(); return {}; }
    if(!output.commit()) fail(QCoreApplication::translate("Cam","无法完成导出：%1").arg(target));
    const auto copied=hashFile(target,cancel);
    if(cancel) return {};
    if(hash.result()!=copied) fail(QCoreApplication::translate("Cam","导出校验失败：%1").arg(target));
    return QString::fromLatin1(copied.toHex());
}
Result exportOriginals(const QString& dbPath, const Source& source, const QStringList& keys, const QString& destination, std::atomic_bool& cancel, const Progress& progress) {
    Result result; Database db(dbPath);
    const QString destRoot=QFileInfo(destination).canonicalFilePath();
    if(destRoot.isEmpty()) fail(QCoreApplication::translate("Cam","导出目标目录不存在。"));
    if(destRoot.compare(source.root,Qt::CaseInsensitive)==0 || isWithin(source.root,destRoot)) fail(QCoreApplication::translate("Cam","请选择录像目录以外的导出位置。"));
    if(identifySource(source.root).id!=source.id) fail(QCoreApplication::translate("Cam","设备已更换。"));
    const QString session="TeslaCam-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+"-"+QUuid::createUuid().toString(QUuid::Id128).left(8);
    result.destination=QDir(destRoot).filePath(session);
    if(!QDir().mkpath(result.destination)) fail(QCoreApplication::translate("Cam","无法创建导出目录。"));
    QJsonArray manifest; int total=0;
    for(const auto& key:keys) total+=int(db.clips(source.id,key).size());
    for(const auto& key:keys) {
        if(cancel) break;
        auto clips=db.clips(source.id,key); if(clips.isEmpty()) continue;
        bool complete=true; QSet<QString> parents;
        for(const auto& clip:clips) {
            if(cancel) { complete=false; break; }
            try {
                const QString input=verifiedPath(source,clip);
                const QString hash=copyVerified(input,QDir(result.destination).filePath(clip.relative),cancel);
                if(cancel) { complete=false; break; }
                verifiedPath(source,clip); // Do not mark exported if the source changed during the copy.
                manifest.append(QJsonObject{{"path",clip.relative},{"sha256",hash},{"bytes",QString::number(clip.size)}});
                parents.insert(QFileInfo(input).absolutePath()); ++result.files; result.bytes+=clip.size;
                if(progress) progress(result.files,total,QCoreApplication::translate("Cam","复制并校验 · %1").arg(clip.relative));
            } catch(const std::exception& e) { complete=false; result.errors << QString::fromUtf8(e.what()); }
        }
        // Include small Tesla event metadata and thumbnails, never follow links.
        if(complete && !cancel) for(const auto& parent:parents) {
            for(const auto& metadata:QDir(parent).entryInfoList(QDir::Files|QDir::NoSymLinks)) {
                const QString ext=metadata.suffix().toLower();
                if(ext!="json" && ext!="jpg" && ext!="png") continue;
                try {
                    Clip m; m.relative=QDir(source.root).relativeFilePath(metadata.absoluteFilePath()); m.size=metadata.size(); m.modified=metadata.lastModified().toMSecsSinceEpoch();
                    const QString target=QDir(result.destination).filePath(m.relative);
                    if(QFileInfo::exists(target)) continue;
                    const QString hash=copyVerified(verifiedPath(source,m),target,cancel);
                    if(cancel) { complete=false; break; }
                    verifiedPath(source,m);
                    manifest.append(QJsonObject{{"path",m.relative},{"sha256",hash},{"bytes",QString::number(m.size)}});
                } catch(const std::exception& e) { complete=false; result.errors << QString::fromUtf8(e.what()); }
            }
        }
        if(complete && !cancel) {
            db.query("INSERT INTO flags(source,event,exported) VALUES(?,?,1) ON CONFLICT(source,event) DO UPDATE SET exported=1",{source.id,key}); ++result.events;
        }
    }
    result.canceled=cancel;
    QSaveFile file(result.destination+"/export-manifest.json");
    if(!file.open(QIODevice::WriteOnly)) fail(QCoreApplication::translate("Cam","无法写入导出清单。"));
    QJsonDocument json(QJsonObject{{"source",source.id},{"completed_events",result.events},{"canceled",result.canceled},{"errors",QJsonArray::fromStringList(result.errors)},{"files",manifest}});
    if(file.write(json.toJson())<0 || !file.commit()) fail(QCoreApplication::translate("Cam","导出清单写入失败。"));
    return result;
}
static void removeEventLeftovers(const Source& source, const QList<Clip>& clips) {
    if(clips.isEmpty()) return;
    const QString parentRel=QFileInfo(clips.first().relative).path();
    const QString folderName=QFileInfo(parentRel).fileName();
    static const QStringList reserved{"recentclips","savedclips","sentryclips","teslacam"};
    if(parentRel.isEmpty() || parentRel=="." || reserved.contains(folderName.toLower())) return;
    for(const auto& clip:clips) if(QFileInfo(clip.relative).path()!=parentRel) return;
    const QString full=QDir(source.root).filePath(parentRel);
    QFileInfo directory(full);
    if(!directory.exists() || !directory.isDir() || linked(directory)) return;
    for(const auto& info:QDir(full).entryInfoList(QDir::Files|QDir::NoSymLinks)) {
        const QString ext=info.suffix().toLower();
        if(ext!="json" && ext!="jpg" && ext!="png") continue;
        Clip metadata; metadata.relative=QDir(source.root).relativeFilePath(info.absoluteFilePath()); metadata.size=info.size(); metadata.modified=info.lastModified().toMSecsSinceEpoch();
        if(!QFile::remove(verifiedPath(source,metadata))) fail(QCoreApplication::translate("Cam","无法删除事件信息：%1").arg(metadata.relative));
    }
    if(QDir(full).entryList(QDir::NoDotAndDotDot|QDir::AllEntries).isEmpty()) QDir().rmdir(full);
}
Result deleteEvents(const QString& dbPath, const Source& source, const QStringList& keys, std::atomic_bool& cancel, const Progress& progress, bool removeLeftovers) {
    Result result; Database db(dbPath); int total=0;
    for(const auto& key:keys) total+=int(db.clips(source.id,key).size());
    for(const auto& key:keys) {
        if(cancel) break;
        auto favorite=db.query("SELECT favorite FROM flags WHERE source=? AND event=?",{source.id,key});
        if(favorite.next() && favorite.value(0).toBool()) { result.errors << QCoreApplication::translate("Cam","已跳过收藏事件：%1").arg(key); continue; }
        const auto clips=db.clips(source.id,key);
        // Preflight the entire event before deleting any of its cameras.
        bool valid=true;
        try { for(const auto& clip:clips) verifiedPath(source,clip); }
        catch(const std::exception& e) { valid=false; result.errors << QString::fromUtf8(e.what()); }
        if(!valid) continue;
        int deleted=0;
        for(const auto& clip:clips) {
            if(cancel) break;
            try {
                const QString path=verifiedPath(source,clip);
                if(!QFile::remove(path)) fail(QCoreApplication::translate("Cam","删除失败：%1").arg(clip.relative));
                db.query("DELETE FROM clips WHERE source=? AND rel=?",{source.id,clip.relative});
                ++deleted; ++result.files; result.bytes+=clip.size;
                if(progress) progress(result.files,total,QCoreApplication::translate("Cam","已删除 · %1").arg(clip.relative));
            } catch(const std::exception& e) { result.errors << QString::fromUtf8(e.what()); break; }
        }
        if(deleted==clips.size() && !clips.isEmpty()) {
            ++result.events;
            db.query("DELETE FROM event_meta WHERE source=? AND event=?",{source.id,key});
            if(removeLeftovers && !cancel) {
                try { removeEventLeftovers(source,clips); }
                catch(const std::exception& e) { result.errors << QString::fromUtf8(e.what()); }
            }
        }
    }
    result.canceled=cancel;
    return result;
}
}
