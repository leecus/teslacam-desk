#include "library.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <stdexcept>
#include <filesystem>

namespace Cam {
static void fail(const QString& message) { throw std::runtime_error(message.toUtf8().constData()); }
QString dataDirectory() {
    QString path = qEnvironmentVariable("TESLACAM_DATA_DIR");
    if (path.isEmpty()) path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!QDir().mkpath(path)) fail("无法创建本地索引目录：" + path);
    return QDir(path).absolutePath();
}
QString humanBytes(qint64 bytes) {
    const QStringList units{"B", "KB", "MB", "GB", "TB"};
    double value = double(bytes); int unit = 0;
    while (value >= 1024 && unit < 4) { value /= 1024; ++unit; }
    return QString::number(value, 'f', unit < 2 ? 0 : 1) + " " + units[unit];
}
QString kindLabel(const QString& kind) {
    if (kind == "sentry") return "哨兵录像";
    if (kind == "saved") return "手动保存";
    if (kind == "recent") return "近期行车 · 推断分组";
    return "其他录像";
}
QString cameraLabel(const QString& camera) {
    static const QMap<QString, QString> labels{{"front", "前方"}, {"back", "后方"},
        {"left_repeater", "左侧"}, {"right_repeater", "右侧"},
        {"left_pillar", "左前"}, {"right_pillar", "右前"}};
    return labels.value(camera, camera);
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
    if (!chosen.exists() || !chosen.isDir() || linked(chosen)) fail("录像目录不存在，或目录是链接。");
    QString root = chosen.canonicalFilePath();
    if (QDir(root).exists("TeslaCam")) root = QFileInfo(QDir(root).filePath("TeslaCam")).canonicalFilePath();
    if (root.isEmpty() || linked(QFileInfo(QDir(chosen.absoluteFilePath()).filePath("TeslaCam"))))
        fail("不能使用链接形式的 TeslaCam 目录。");
    // Reject links in all ancestors; actions must never traverse junctions to another target.
    QString ancestor = root;
    while (true) {
        if (linked(QFileInfo(ancestor))) fail("录像目录路径含符号链接或目录联接。");
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
        fail("无法读取磁盘身份，请重新连接设备。");
    if (GetVolumeNameForVolumeMountPointW(volumeRoot, guid, MAX_PATH)) device = QString::fromWCharArray(guid);
    device += ":" + QString::number(serial, 16);
#else
    device = QString::fromUtf8(storage.device());
#endif
    const QString relative = QDir(storage.rootPath()).relativeFilePath(root).toCaseFolded();
    Source source;
    source.id = QString::fromLatin1(QCryptographicHash::hash((device + "/" + relative).toUtf8(), QCryptographicHash::Sha256).toHex());
    source.root = root;
    source.name = (storage.name().isEmpty() ? "录像设备" : storage.name()) + " · " + QDir::toNativeSeparators(root);
    source.status = "尚未扫描";
    return source;
}
QString verifiedPath(const Source& source, const Clip& clip, bool checkFingerprint) {
    if (identifySource(source.root).id != source.id) fail("磁盘身份已变化，已停止操作。");
    const QString rel = QDir::fromNativeSeparators(clip.relative);
    if (rel.isEmpty() || QDir::isAbsolutePath(rel) || rel.split('/').contains("..")) fail("索引文件路径无效。");
    const QString full = QDir(source.root).absoluteFilePath(rel);
    QString parent = full;
    while (isWithin(source.root, parent)) {
        if (linked(QFileInfo(parent))) fail("文件路径已变为链接：" + rel);
        parent = QFileInfo(parent).dir().absolutePath();
    }
    QFileInfo info(full);
    if (!info.exists() || !info.isFile() || !isWithin(source.root, info.canonicalFilePath())) fail("文件已离线或不存在：" + rel);
    if (checkFingerprint && (info.size() != clip.size || info.lastModified().toMSecsSinceEpoch() != clip.modified))
        fail("文件已变化，请更新索引后重试：" + rel);
    return info.canonicalFilePath();
}
QString thumbnailPath(const Source& source, const Clip& clip) {
    const QByteArray value = (source.id + "/" + clip.relative + "/" + QString::number(clip.size) + "/" + QString::number(clip.modified)).toUtf8();
    const QString dir = dataDirectory() + "/thumbnails";
    QDir().mkpath(dir);
    return dir + "/" + QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex()) + ".jpg";
}
Database::Database(const QString& path) {
    connection = QUuid::createUuid().toString(QUuid::WithoutBraces);
    sql = QSqlDatabase::addDatabase("QSQLITE", connection);
    sql.setDatabaseName(path);
    sql.setConnectOptions("QSQLITE_BUSY_TIMEOUT=10000");
    if (!sql.open()) fail("无法打开索引：" + sql.lastError().text());
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA foreign_keys=ON");
    exec("CREATE TABLE IF NOT EXISTS sources(id TEXT PRIMARY KEY,root TEXT,name TEXT,last_scan TEXT,status TEXT)");
    exec("CREATE TABLE IF NOT EXISTS clips(source TEXT,rel TEXT,event TEXT,kind TEXT,stamp TEXT,camera TEXT,bytes INTEGER,modified INTEGER,seen TEXT,PRIMARY KEY(source,rel))");
    exec("CREATE INDEX IF NOT EXISTS clips_event ON clips(source,event,stamp)");
    exec("CREATE INDEX IF NOT EXISTS clips_time ON clips(source,stamp,kind)");
    exec("CREATE TABLE IF NOT EXISTS flags(source TEXT,event TEXT,favorite INTEGER DEFAULT 0,exported INTEGER DEFAULT 0,PRIMARY KEY(source,event))");
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
    return result;
}
void Database::saveSource(const Source& s) {
    query("INSERT INTO sources VALUES(?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET root=excluded.root,name=excluded.name,last_scan=excluded.last_scan,status=excluded.status",
          {s.id, s.root, s.name, s.lastScan, s.status});
}
QList<Event> Database::events(const QString& id, const QString& kind, const QString& from, const QString& to, int state, const QString& search, bool oldest) {
    QString text = "SELECT c.event,c.kind,MIN(c.stamp),MAX(c.stamp),SUM(c.bytes),COUNT(*),COUNT(DISTINCT c.camera),COALESCE(f.favorite,0),COALESCE(f.exported,0) FROM clips c LEFT JOIN flags f ON f.source=c.source AND f.event=c.event WHERE c.source=?";
    QVariantList args{id};
    if (!kind.isEmpty()) { text += " AND c.kind=?"; args << kind; }
    if (state == 1) text += " AND f.favorite=1";
    if (state == 2) text += " AND f.exported=1";
    if (state == 3) text += " AND COALESCE(f.exported,0)=0";
    text += " GROUP BY c.event HAVING 1=1";
    // Filter complete events by time overlap; never drop sibling cameras from an action.
    if (!from.isEmpty()) { text += " AND datetime(MAX(c.stamp),'+60 seconds')>?"; args << from; }
    if (!to.isEmpty()) { text += " AND MIN(c.stamp)<=?"; args << to; }
    if (!search.isEmpty()) { text += " AND (instr(lower(c.event),lower(?))>0 OR instr(MIN(c.stamp),?)>0)"; args << search << search; }
    text += oldest ? " ORDER BY MIN(c.stamp) ASC" : " ORDER BY MIN(c.stamp) DESC";
    auto q = query(text, args); QList<Event> result;
    while (q.next()) {
        Event e; e.key=q.value(0).toString(); e.kind=q.value(1).toString(); e.start=q.value(2).toString(); e.end=q.value(3).toString();
        e.bytes=q.value(4).toLongLong(); e.files=q.value(5).toInt(); e.cameras=q.value(6).toInt(); e.favorite=q.value(7).toBool(); e.exported=q.value(8).toBool(); result << e;
    }
    return result;
}
QList<Clip> Database::clips(const QString& id, const QString& event) {
    auto q = query("SELECT rel,event,kind,camera,stamp,bytes,modified FROM clips WHERE source=? AND event=? ORDER BY stamp,camera", {id,event});
    QList<Clip> result;
    while(q.next()) result.append({q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),q.value(4).toString(),q.value(5).toLongLong(),q.value(6).toLongLong()});
    return result;
}
void Database::setFavorite(const QString& id, const QString& event, bool value) {
    query("INSERT INTO flags(source,event,favorite) VALUES(?,?,?) ON CONFLICT(source,event) DO UPDATE SET favorite=excluded.favorite", {id,event,value});
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
    if (!info.isReadable() || linked(info)) fail("目录不可读取或含链接："+dir);
    std::error_code ec;
    const std::filesystem::path native(dir.toStdWString());
    std::filesystem::directory_iterator iterator(native,ec),end;
    if(ec) fail("无法枚举目录（不会移除旧索引）："+dir+" · "+QString::fromStdString(ec.message()));
    while(iterator!=end) {
        if (cancel) break;
        const QFileInfo entry(QString::fromStdWString(iterator->path().wstring()));
        if (!linked(entry)) {
            if (entry.isDir()) enumerate(entry.absoluteFilePath(),files,cancel,progress,visited);
            else if(entry.suffix().compare("mp4",Qt::CaseInsensitive)==0) {
                files << entry.absoluteFilePath();
                if (++visited%100==0 && progress) progress(visited,0,"发现视频文件，正在枚举目录…");
            }
        }
        iterator.increment(ec);
        if(ec) fail("目录读取中断（不会移除旧索引）："+dir);
    }
}
Result scan(const QString& dbPath, const Source& source, bool rebuild, std::atomic_bool& cancel, const Progress& progress) {
    Result result; Database db(dbPath); Source current=source;
    for(const auto& old:db.sources()) if(old.id==source.id) current.lastScan=old.lastScan;
    if(identifySource(source.root).id!=source.id) fail("设备身份不匹配。");
    current.status="扫描中（列表可能不完整）"; db.saveSource(current);
    QStringList paths; int visited=0;
    try {
        enumerate(source.root,paths,cancel,progress,visited);
        paths.sort(Qt::CaseInsensitive);
        const QString generation=QUuid::createUuid().toString();
        QHash<QString,QPair<qint64,qint64>> known;
        {
            auto q=db.query("SELECT rel,bytes,modified FROM clips WHERE source=?",{source.id});
            while(q.next()) known.insert(q.value(0).toString(),{q.value(1).toLongLong(),q.value(2).toLongLong()});
        }
        bool transaction=false;
        try {
            for(int i=0;i<paths.size() && !cancel;++i) {
                if(!transaction) { if(!db.sql.transaction()) fail(db.sql.lastError().text()); transaction=true; }
                QFileInfo info(paths[i]);
                if(!info.exists() || !info.isFile()) fail("扫描时文件消失，请重新扫描："+paths[i]);
                const Clip c=describe(source.root,info);
                // Validate before indexing: a directory could have changed since enumeration.
                verifiedPath(source,c);
                const auto previous=known.constFind(c.relative);
                const bool changed=previous==known.cend() || previous->first!=c.size || previous->second!=c.modified;
                if(!changed && !rebuild)
                    db.query("UPDATE clips SET seen=? WHERE source=? AND rel=?",{generation,source.id,c.relative});
                else {
                    db.query("INSERT INTO clips VALUES(?,?,?,?,?,?,?,?,?) ON CONFLICT(source,rel) DO UPDATE SET event=excluded.event,kind=excluded.kind,stamp=excluded.stamp,camera=excluded.camera,bytes=excluded.bytes,modified=excluded.modified,seen=excluded.seen",
                        {source.id,c.relative,c.event,c.kind,c.stamp,c.camera,c.size,c.modified,generation});
                    if(changed) {
                        db.query("UPDATE flags SET exported=0 WHERE source=? AND event=?",{source.id,c.event});
                        ++result.changed;
                    }
                }
                ++result.files; result.bytes+=c.size;
                if(result.files%100==0 || i==paths.size()-1) {
                    if(!db.sql.commit()) fail(db.sql.lastError().text()); transaction=false;
                    if(progress) progress(result.files,int(paths.size()),"建立索引 · "+c.relative);
                }
            }
            if(transaction && !db.sql.commit()) fail(db.sql.lastError().text());
        } catch(...) { if(transaction) db.sql.rollback(); throw; }
        result.canceled=cancel;
        if(!cancel) {
            if(identifySource(source.root).id!=source.id) fail("设备已断开或被更换。");
            auto q=db.query("DELETE FROM clips WHERE source=? AND seen<>?",{source.id,generation}); result.removed=int(q.numRowsAffected());
            current.lastScan=QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"); current.status="索引已完成";
        } else current.status="扫描已中断 · 可继续更新";
        db.saveSource(current);
    } catch(...) { current.status="扫描失败 · 保留已有索引"; db.saveSource(current); throw; }
    result.events=int(db.events(source.id).size());
    return result;
}

static QByteArray hashFile(const QString& path, std::atomic_bool& cancel) {
    QFile file(path); if(!file.open(QIODevice::ReadOnly)) fail("无法读取："+path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while(!file.atEnd()) {
        if(cancel) return {};
        const QByteArray data=file.read(1024*1024);
        if(data.isEmpty() && file.error()!=QFileDevice::NoError) fail("读取失败："+path);
        hash.addData(data);
    }
    return hash.result();
}
static QString copyVerified(const QString& path, const QString& target, std::atomic_bool& cancel) {
    QFile input(path); if(!input.open(QIODevice::ReadOnly)) fail("无法读取原文件："+path);
    if(QFileInfo::exists(target)) fail("目标已存在，禁止覆盖："+target);
    if(!QDir().mkpath(QFileInfo(target).absolutePath())) fail("无法创建导出目录。");
    QSaveFile output(target); if(!output.open(QIODevice::WriteOnly)) fail("无法写入："+target);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while(!input.atEnd()) {
        if(cancel) { output.cancelWriting(); return {}; }
        QByteArray block=input.read(1024*1024);
        if(block.isEmpty() && input.error()!=QFileDevice::NoError) fail("原文件读取失败："+path);
        hash.addData(block);
        if(output.write(block)!=block.size()) fail("导出失败，可能空间不足："+target);
    }
    if(cancel) { output.cancelWriting(); return {}; }
    if(!output.commit()) fail("无法完成导出："+target);
    const auto copied=hashFile(target,cancel);
    if(cancel) return {};
    if(hash.result()!=copied) fail("导出校验失败："+target);
    return QString::fromLatin1(copied.toHex());
}
Result exportOriginals(const QString& dbPath, const Source& source, const QStringList& keys, const QString& destination, std::atomic_bool& cancel, const Progress& progress) {
    Result result; Database db(dbPath);
    const QString destRoot=QFileInfo(destination).canonicalFilePath();
    if(destRoot.isEmpty()) fail("导出目标目录不存在。");
    if(destRoot.compare(source.root,Qt::CaseInsensitive)==0 || isWithin(source.root,destRoot)) fail("请选择录像目录以外的导出位置。");
    if(identifySource(source.root).id!=source.id) fail("设备已更换。");
    const QString session="TeslaCam-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+"-"+QUuid::createUuid().toString(QUuid::Id128).left(8);
    result.destination=QDir(destRoot).filePath(session);
    if(!QDir().mkpath(result.destination)) fail("无法创建导出目录。");
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
                if(progress) progress(result.files,total,"复制并校验 · "+clip.relative);
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
    if(!file.open(QIODevice::WriteOnly)) fail("无法写入导出清单。");
    QJsonDocument json(QJsonObject{{"source",source.id},{"completed_events",result.events},{"canceled",result.canceled},{"errors",QJsonArray::fromStringList(result.errors)},{"files",manifest}});
    if(file.write(json.toJson())<0 || !file.commit()) fail("导出清单写入失败。");
    return result;
}
Result deleteEvents(const QString& dbPath, const Source& source, const QStringList& keys, std::atomic_bool& cancel, const Progress& progress) {
    Result result; Database db(dbPath); int total=0;
    for(const auto& key:keys) total+=int(db.clips(source.id,key).size());
    for(const auto& key:keys) {
        if(cancel) break;
        auto favorite=db.query("SELECT favorite FROM flags WHERE source=? AND event=?",{source.id,key});
        if(favorite.next() && favorite.value(0).toBool()) { result.errors << "已跳过收藏事件："+key; continue; }
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
                if(!QFile::remove(path)) fail("删除失败："+clip.relative);
                db.query("DELETE FROM clips WHERE source=? AND rel=?",{source.id,clip.relative});
                ++deleted; ++result.files; result.bytes+=clip.size;
                if(progress) progress(result.files,total,"已删除 · "+clip.relative);
            } catch(const std::exception& e) { result.errors << QString::fromUtf8(e.what()); break; }
        }
        if(deleted==clips.size() && !clips.isEmpty()) ++result.events;
    }
    result.canceled=cancel;
    return result;
}
}
