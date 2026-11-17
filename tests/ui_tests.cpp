#include "window.h"
#include <QtTest>

class UiTests:public QObject {
    Q_OBJECT
    QString ffmpeg;
    static QString databasePath(const QTemporaryDir& temp) {
        const QString data=temp.path()+"/index"; QDir().mkpath(data); qputenv("TESLACAM_DATA_DIR",data.toUtf8()); return data+"/index.sqlite";
    }
    void video(const QString& target) {
        QDir().mkpath(QFileInfo(target).absolutePath());
        QProcess process; process.start(ffmpeg,{"-hide_banner","-loglevel","error","-nostdin","-f","lavfi","-i","testsrc2=size=320x180:rate=15","-t","2","-c:v","libx264",target});
        QVERIFY(process.waitForFinished(30000)); QCOMPARE(process.exitCode(),0);
    }
private slots:
    void initTestCase() {
        ffmpeg=QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../bin/ffmpeg.exe");
        QVERIFY(QFileInfo::exists(ffmpeg));
        qputenv("PATH",QFileInfo(ffmpeg).absolutePath().toUtf8()+QDir::listSeparator().toLatin1()+qgetenv("PATH"));
    }
    void startupRecoversMalformedIndex() {
        QTemporaryDir temp; const QString path=databasePath(temp);
        QFile damaged(path); QVERIFY(damaged.open(QIODevice::WriteOnly)); damaged.write("broken index content that must be retained"); damaged.close();
        MainWindow window; window.show(); QTest::qWait(50);
        QCOMPARE(window.findChildren<QTableWidget*>().size(),1);
        QVERIFY(QDir(temp.path()+"/index/recovery").exists());
        Cam::Database db(path); auto q=db.query("PRAGMA quick_check"); QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("ok"));
    }
    void closingWindowWaitsForThumbnailWorker() {
        QTemporaryDir temp; const QString path=databasePath(temp),root=temp.path()+"/TeslaCam";
        const QString sample=root+"/SavedClips/event0/2026-11-12_14-00-00-front.mp4"; video(sample);
        for(int i=1;i<30;++i) {
            const QString target=root+"/SavedClips/event"+QString::number(i)+"/2026-11-12_14-00-00-front.mp4";
            QDir().mkpath(QFileInfo(target).absolutePath()); QVERIFY(QFile::copy(sample,target));
        }
        const auto source=Cam::identifySource(root); std::atomic_bool cancel=false; Cam::scan(path,source,false,cancel);
        auto window=new MainWindow(root); window->show();
        QProgressBar* task=nullptr;
        for(auto progress:window->findChildren<QProgressBar*>()) if(progress->accessibleName()==QString::fromUtf8("当前任务进度")) task=progress;
        QVERIFY(task); QTRY_VERIFY_WITH_TIMEOUT(task->isVisible(),3000);
        delete window; QTest::qWait(100);
        Cam::Database db(path); auto check=db.query("PRAGMA quick_check"); QVERIFY(check.next()); QCOMPARE(check.value(0).toString(),QString("ok"));
    }
    void mediaCallbacksDoNotOutliveTheirLanes() {
        QTemporaryDir temp; databasePath(temp); const QString root=temp.path()+"/TeslaCam";
        const QString file=root+"/SavedClips/event/2026-11-12_14-00-00-front.mp4"; video(file);
        const auto source=Cam::identifySource(root); const QFileInfo info(file);
        Cam::Clip clip{"SavedClips/event/2026-11-12_14-00-00-front.mp4","SavedClips/event","saved","front","2026-11-12 14:00:00",info.size(),info.lastModified().toMSecsSinceEpoch()};
        EventPlayer player; player.show();
        for(int i=0;i<8;++i) {player.load(source,{clip}); QTest::qWait(15); player.clear(); QCoreApplication::processEvents();}
        QTest::qWait(100); QCOMPARE(player.findChildren<QMediaPlayer*>().size(),0);
    }
};
QTEST_MAIN(UiTests)
#include "ui_tests.moc"
