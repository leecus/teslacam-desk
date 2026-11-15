#include "library.h"
#include "media.h"
#include <QtTest>

class CoreTests:public QObject {
    Q_OBJECT
    static void write(const QString& path,const QByteArray& content="video bytes") {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath())); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(content),content.size());
    }
private slots:
    void indexGroupingAndWholeEventFilters() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto root=temp.path()+"/中文 U盘/TeslaCam";
        write(root+"/SentryClips/2026-11-12_14-00-00/2026-11-12_14-00-00-front.mp4");
        write(root+"/SentryClips/2026-11-12_14-00-00/2026-11-12_14-01-00-back.mp4");
        write(root+"/RecentClips/2026-11-12_15-01-00-front.MP4");
        write(root+"/RecentClips/2026-11-12_15-02-00-right_repeater.mp4");
        write(root+"/RecentClips/2026-11-12_15-12-00-front.mp4");
        write(root+"/SavedClips/2026-11-13_01-00-00/2026-11-13_01-00-00-left_pillar.mp4");
        const auto source=Cam::identifySource(temp.path()+"/中文 U盘"); QCOMPARE(source.root,QFileInfo(root).canonicalFilePath());
        const auto path=temp.path()+"/index.sqlite"; std::atomic_bool cancel=false;
        auto result=Cam::scan(path,source,false,cancel); QCOMPARE(result.files,6); QCOMPARE(result.events,4); QCOMPARE(result.changed,6);
        Cam::Database db(path); auto sentry=db.events(source.id,"sentry","2026-11-12 14:00:30","2026-11-12 14:00:40");
        QCOMPARE(sentry.size(),1); QCOMPARE(sentry.first().files,2); QCOMPARE(sentry.first().cameras,2);
        QCOMPARE(db.events(source.id,"recent").size(),2); QVERIFY(!db.sources().first().lastScan.isEmpty());
        result=Cam::scan(path,source,false,cancel); QCOMPARE(result.changed,0); QCOMPARE(result.files,6);
        result=Cam::scan(path,source,true,cancel); QCOMPARE(result.files,6); QCOMPARE(result.changed,0);
    }
    void cancellationKeepsOldIndexAndResumes() {
        QTemporaryDir temp; const auto root=temp.path()+"/TeslaCam";
        for(int i=0;i<120;++i) write(root+"/SavedClips/event/2026-11-12_14-00-00-camera"+QString::number(i)+".mp4");
        const auto source=Cam::identifySource(root); const auto path=temp.path()+"/index.sqlite"; std::atomic_bool cancel=false;
        Cam::scan(path,source,false,cancel); Cam::Database db(path); const auto completed=db.sources().first().lastScan;
        QVERIFY(QFile::remove(root+"/SavedClips/event/2026-11-12_14-00-00-camera0.mp4"));
        cancel=true; auto result=Cam::scan(path,source,false,cancel); QVERIFY(result.canceled); QCOMPARE(db.clips(source.id,"SavedClips/event").size(),120); QCOMPARE(db.sources().first().lastScan,completed);
        cancel=false; result=Cam::scan(path,source,false,cancel); QCOMPARE(result.removed,1); QCOMPARE(db.clips(source.id,"SavedClips/event").size(),119);
        cancel=false; result=Cam::scan(path,source,true,cancel,[&cancel](int done,int total,const QString&){if(total>0 && done>=100) cancel=true;});
        QVERIFY(result.canceled); QCOMPARE(db.clips(source.id,"SavedClips/event").size(),119);
        cancel=false; result=Cam::scan(path,source,false,cancel); QVERIFY(!result.canceled); QCOMPARE(result.files,119);
    }
    void exportPreservesOriginalsMetadataAndHashes() {
        QTemporaryDir temp; const auto root=temp.path()+"/TeslaCam";
        write(root+"/SentryClips/event/2026-11-12_14-00-00-front.mp4","unique original footage");
        write(root+"/SentryClips/event/event.json","{\"timestamp\":\"2026-11-12T14:00:03+08:00\"}");
        QDir().mkpath(temp.path()+"/exports"); const auto source=Cam::identifySource(root); const auto path=temp.path()+"/index.sqlite"; std::atomic_bool cancel=false;
        Cam::scan(path,source,false,cancel); auto result=Cam::exportOriginals(path,source,{"SentryClips/event"},temp.path()+"/exports",cancel);
        QVERIFY2(result.errors.isEmpty(),qPrintable(result.errors.join('\n'))); QCOMPARE(result.events,1); QCOMPARE(result.files,1);
        QFile copied(result.destination+"/SentryClips/event/2026-11-12_14-00-00-front.mp4"); QVERIFY(copied.open(QIODevice::ReadOnly)); QCOMPARE(copied.readAll(),QByteArray("unique original footage"));
        QVERIFY(QFileInfo::exists(root+"/SentryClips/event/2026-11-12_14-00-00-front.mp4")); QVERIFY(QFileInfo::exists(result.destination+"/SentryClips/event/event.json"));
        QFile manifest(result.destination+"/export-manifest.json"); QVERIFY(manifest.open(QIODevice::ReadOnly)); auto json=QJsonDocument::fromJson(manifest.readAll()).object();
        QCOMPARE(json["files"].toArray().size(),2); QCOMPARE(json["completed_events"].toInt(),1);
        auto entry=json["files"].toArray().first().toObject(); QCOMPARE(entry["sha256"].toString(),QString::fromLatin1(QCryptographicHash::hash("unique original footage",QCryptographicHash::Sha256).toHex()));
        Cam::Database db(path); QVERIFY(db.events(source.id).first().exported);
        Cam::scan(path,source,true,cancel); QVERIFY(db.events(source.id).first().exported);
        write(root+"/SentryClips/event/2026-11-12_14-00-00-back.mp4","new camera"); Cam::scan(path,source,false,cancel); QVERIFY(!db.events(source.id).first().exported);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,Cam::exportOriginals(path,source,{"SentryClips/event"},root,cancel));
    }
    void favoritesAndChangedFilesPreventDeletion() {
        QTemporaryDir temp; const auto root=temp.path()+"/TeslaCam";
        const auto front=root+"/SavedClips/event/2026-11-12_14-00-00-front.mp4", back=root+"/SavedClips/event/2026-11-12_14-00-00-back.mp4";
        write(front); write(back); write(root+"/SavedClips/event/event.json","{}");
        const auto source=Cam::identifySource(root); const auto path=temp.path()+"/index.sqlite"; std::atomic_bool cancel=false;
        Cam::scan(path,source,false,cancel); Cam::Database db(path); db.setFavorite(source.id,"SavedClips/event",true);
        auto result=Cam::deleteEvents(path,source,{"SavedClips/event"},cancel); QCOMPARE(result.files,0); QVERIFY(!result.errors.isEmpty()); QVERIFY(QFileInfo::exists(front));
        db.setFavorite(source.id,"SavedClips/event",false); write(front,"changed size and content");
        result=Cam::deleteEvents(path,source,{"SavedClips/event"},cancel); QCOMPARE(result.files,0); QVERIFY(QFileInfo::exists(back));
        Cam::scan(path,source,false,cancel); cancel=true; result=Cam::deleteEvents(path,source,{"SavedClips/event"},cancel); QCOMPARE(result.files,0);
        cancel=false; result=Cam::deleteEvents(path,source,{"SavedClips/event"},cancel); QCOMPARE(result.files,2); QCOMPARE(result.events,1); QVERIFY(!QFileInfo::exists(front)); QVERIFY(!QFileInfo::exists(back));
        QVERIFY(QFileInfo::exists(root+"/SavedClips/event/event.json")); QCOMPARE(db.events(source.id).size(),0);
    }
    void rejectsWrongDeviceAndTraversal() {
        QTemporaryDir temp; const auto root=temp.path()+"/TeslaCam"; write(root+"/RecentClips/2026-11-12_14-00-00-front.mp4");
        auto source=Cam::identifySource(root); const auto path=temp.path()+"/index.sqlite"; std::atomic_bool cancel=false;
        Cam::scan(path,source,false,cancel); Cam::Database db(path); auto clip=db.clips(source.id,db.events(source.id).first().key).first();
        clip.relative="../outside.mp4"; QVERIFY_THROWS_EXCEPTION(std::runtime_error,Cam::verifiedPath(source,clip));
        source.id="another-device"; QVERIFY_THROWS_EXCEPTION(std::runtime_error,Cam::scan(path,source,false,cancel));
        QVERIFY(!Cam::isWithin(root,root+"-other/file.mp4")); QVERIFY(Cam::isWithin(root,root+"/file.mp4"));
    }
    void trimsAcrossClipsAndRejectsGaps() {
        const auto executable=QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../bin/ffmpeg.exe");
        QVERIFY2(QFileInfo::exists(executable),"FFmpeg CLI must be installed before running media integration tests");
        QTemporaryDir temp; const QString root=temp.path()+"/中文 测试/TeslaCam";
        QDir().mkpath(root+"/SavedClips/event"); QDir().mkpath(temp.path()+"/exports");
        std::atomic_bool cancel=false; QString error;
        const QString first=root+"/SavedClips/event/2026-11-12_14-00-00-front.mp4", second=root+"/SavedClips/event/2026-11-12_14-00-02-front.mp4";
        QVERIFY2(Cam::runFfmpeg(executable,{"-hide_banner","-loglevel","error","-y","-f","lavfi","-i","testsrc2=size=320x180:rate=15","-t","2","-c:v","libx264","-pix_fmt","yuv420p",first},cancel,&error),qPrintable(error));
        QVERIFY(QFile::copy(first,second));
        const auto source=Cam::identifySource(root); const auto path=temp.path()+"/index.sqlite";
        Cam::scan(path,source,false,cancel); Cam::Database db(path); const auto clips=db.clips(source.id,"SavedClips/event");
        QCOMPARE(Cam::probeDuration(executable,first,cancel),2000);
        const auto result=Cam::exportRange(source,clips,"front",1,3,temp.path()+"/exports",executable,cancel);
        QCOMPARE(result.files,1); QVERIFY(QFileInfo::exists(result.destination)); QVERIFY(qAbs(Cam::probeDuration(executable,result.destination,cancel)-2000)<=150);
        auto gapped=clips; gapped[1].stamp="2026-11-12 14:01:00";
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,Cam::exportRange(source,gapped,"front",1,3,temp.path()+"/exports",executable,cancel));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,Cam::exportRange(source,clips,"back",0,1,temp.path()+"/exports",executable,cancel));
        cancel=true; const auto stopped=Cam::exportRange(source,clips,"front",0,2,temp.path()+"/exports",executable,cancel); QVERIFY(stopped.canceled); QCOMPARE(stopped.files,0);
    }
    void indexesMetadataDurationAndPreviewFrame() {
        QTemporaryDir temp; const auto root=temp.path()+"/TeslaCam";
        write(root+"/SentryClips/event/2026-11-12_14-00-00-back.mp4","back");
        write(root+"/SentryClips/event/2026-11-12_14-00-00-front.mp4","front");
        write(root+"/SentryClips/event/event.json","{\"timestamp\":\"2026-11-12T14:00:08\",\"reason\":\"user_interaction_honk\",\"city\":\"上海\"}");
        const auto source=Cam::identifySource(root); const auto path=temp.path()+"/index.sqlite"; std::atomic_bool cancel=false;
        Cam::scan(path,source,false,cancel); Cam::Database db(path); const auto events=db.events(source.id);
        QCOMPARE(events.size(),1); QCOMPARE(events.first().reason,"user_interaction_honk"); QCOMPARE(events.first().city,"上海"); QCOMPARE(events.first().trigger,"2026-11-12T14:00:08");
        QCOMPARE(Cam::reasonLabel(events.first().reason),"鸣笛"); QCOMPARE(db.events(source.id,{},{},{},0,"上海").size(),1); QCOMPARE(db.events(source.id,{},{},{},0,"鸣笛").size(),1);
        const auto clips=db.clips(source.id,events.first().key); const auto target=Cam::previewTarget(clips,events.first().trigger);
        QCOMPARE(target.clip.camera,"front"); QVERIFY(target.offsetMs>=7000 && target.offsetMs<=9000);
        for(const auto& clip:clips) db.setClipDuration(source.id,clip.relative,5000);
        Cam::scan(path,source,true,cancel);
        for(const auto& clip:db.clips(source.id,events.first().key)) QCOMPARE(clip.duration,5000);
        QCOMPARE(db.events(source.id,"sentry","2026-11-12 14:00:20","2026-11-12 14:00:30").size(),0);
        QCOMPARE(db.events(source.id,"sentry","2026-11-12 14:00:02","2026-11-12 14:00:04").size(),1);
        write(root+"/SentryClips/event/2026-11-12_14-00-00-front.mp4","front changed");
        Cam::scan(path,source,false,cancel);
        for(const auto& clip:db.clips(source.id,events.first().key)) if(clip.camera=="front") QCOMPARE(clip.duration,0); else QCOMPARE(clip.duration,5000);
    }
    void deleteCanRemoveEventFolderButNotRecentRoot() {
        QTemporaryDir temp; const auto root=temp.path()+"/TeslaCam";
        write(root+"/SavedClips/keep/2026-11-12_14-00-00-front.mp4"); write(root+"/SavedClips/keep/event.json","{}");
        write(root+"/SavedClips/drop/2026-11-12_14-05-00-front.mp4"); write(root+"/SavedClips/drop/event.json","{}"); write(root+"/SavedClips/drop/thumb.jpg","jpg");
        write(root+"/RecentClips/2026-11-12_15-01-00-front.mp4"); write(root+"/RecentClips/event.json","{}");
        const auto source=Cam::identifySource(root); const auto path=temp.path()+"/index.sqlite"; std::atomic_bool cancel=false;
        Cam::scan(path,source,false,cancel); Cam::Database db(path); QString kept,dropped,recent;
        for(const auto& event:db.events(source.id)) { if(event.key.endsWith("keep")) kept=event.key; else if(event.key.endsWith("drop")) dropped=event.key; else if(event.kind=="recent") recent=event.key; }
        QVERIFY(!kept.isEmpty()); QVERIFY(!dropped.isEmpty()); QVERIFY(!recent.isEmpty());
        auto result=Cam::deleteEvents(path,source,{kept},cancel,{},false); QCOMPARE(result.files,1); QVERIFY(QFileInfo::exists(root+"/SavedClips/keep/event.json"));
        result=Cam::deleteEvents(path,source,{dropped,recent},cancel,{},true); QCOMPARE(result.errors.isEmpty(),true); QCOMPARE(result.files,2);
        QVERIFY(!QFileInfo::exists(root+"/SavedClips/drop/event.json")); QVERIFY(!QFileInfo::exists(root+"/SavedClips/drop/thumb.jpg")); QVERIFY(!QFileInfo::exists(root+"/SavedClips/drop"));
        QVERIFY(QFileInfo::exists(root+"/RecentClips/event.json")); QVERIFY(QFileInfo::exists(root+"/RecentClips")); QVERIFY(!QFileInfo::exists(root+"/RecentClips/2026-11-12_15-01-00-front.mp4"));
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
