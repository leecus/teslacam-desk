#include "window.h"
#include "theme.h"
#include "i18n.h"
#include <cstdio>

class Application final : public QApplication {
public:
    Application(int& argc,char** argv):QApplication(argc,argv) {}
    bool notify(QObject* receiver,QEvent* event) override {
        try {return QApplication::notify(receiver,event);}
        catch(const std::exception& e) {report(QString::fromUtf8(e.what()));}
        catch(...) {report(QCoreApplication::translate("main","未知的界面错误"));}
        return false;
    }
private:
    bool reporting=false;
    void report(const QString& message) {
        qWarning().noquote()<<"Unhandled UI operation:"<<message;
        if(reporting) return;
        reporting=true;
        QTimer::singleShot(0,this,[this,message] {
            try {
                for(auto widget:topLevelWidgets()) if(auto window=qobject_cast<MainWindow*>(widget)) window->showFailure(message);
                QMessageBox::warning(nullptr,QCoreApplication::translate("main","操作失败"),message);
            } catch(...) {}
            reporting=false;
        });
    }
};

int main(int argc,char* argv[]) {
    Application app(argc,argv); app.setApplicationName("TeslaCamManager"); app.setOrganizationName("LocalTools"); app.setApplicationVersion(TESLACAM_VERSION); app.setStyle("Fusion");
    app.setWindowIcon(QIcon(":/icons/teslacam.svg"));
    QCommandLineParser parser; parser.setApplicationDescription(QCoreApplication::translate("main","特斯拉影像管家：本地录像索引与事件管理")); parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"source",QCoreApplication::translate("main","选择初始录像目录（不会自动扫描）"),"path"});
    parser.addOption({"theme",QCoreApplication::translate("main","启动时使用 light 或 dark 外观（不写入设置）"),"light|dark"});
    parser.addOption({"lang",QCoreApplication::translate("main","启动时使用 zh 或 en 界面（不写入设置）"),"zh|en"});
    parser.addOption({"screenshot",QCoreApplication::translate("main","启动后把窗口渲染为 PNG 并退出，用于检查界面"),"path"});
    parser.addOption({"scan",QCoreApplication::translate("main","扫描 --source 指定的目录并打印统计后退出，用于批量与性能验证")});
    parser.addOption({"smoke-test",QCoreApplication::translate("main","启动窗口并自动退出，用于检查运行依赖")}); parser.process(app);
    QString indexDirectory;
    try {indexDirectory=Cam::dataDirectory();}
    catch(const std::exception& e) {
        if(parser.isSet("scan") || parser.isSet("smoke-test")) std::fprintf(stderr,"%s\n",e.what());
        else QMessageBox::warning(nullptr,QCoreApplication::translate("main","启动失败"),QString::fromUtf8(e.what()));
        return 1;
    }
    QLockFile indexLock(indexDirectory+"/application.lock");
    if(!indexLock.tryLock(0)) {
        if(parser.isSet("scan") || parser.isSet("smoke-test")) std::fprintf(stderr,"This index is already open in another application instance.\n");
        else QMessageBox::information(nullptr,QCoreApplication::translate("main","应用已在运行"),QCoreApplication::translate("main","请使用已打开的窗口，或先关闭其他实例。"));
        return 3;
    }
    I18n::install(parser.isSet("lang")?I18n::parse(parser.value("lang")):I18n::load());
    Theme::apply(parser.isSet("theme")?Theme::parse(parser.value("theme")):Theme::load());
    if(parser.isSet("scan")) {
        const QString path=parser.value("source");
        if(path.isEmpty()) {std::fprintf(stderr,"--scan requires --source <folder>\n"); return 2;}
        try {
            Cam::Source source=Cam::identifySource(path);
            const QString dbPath=Cam::dataDirectory()+"/index.sqlite";
            const auto recovered=Cam::recoverIndex(dbPath);
            if(!recovered.backupDirectory.isEmpty()) std::fprintf(stderr,"Damaged index preserved at: %s\n",qUtf8Printable(recovered.backupDirectory));
            {
                Cam::Database db(dbPath);
                for(const auto& old:db.sources()) if(old.id==source.id) {source.lastScan=old.lastScan; source.status=old.status;}
                db.saveSource(source);
            }
            std::atomic_bool canceled=false;
            QElapsedTimer timer; timer.start();
            const Cam::Result result=Cam::scan(dbPath,source,false,canceled,[&](int done,int total,const QString& message){
                if(total>0 && done%500==0) std::fprintf(stderr,"%d/%d %s\n",done,total,qUtf8Printable(message));
            });
            std::printf("{\"files\":%d,\"events\":%d,\"bytes\":%lld,\"changed\":%d,\"skipped\":%d,\"ms\":%lld}\n",
                        result.files,result.events,result.bytes,result.changed,result.skipped,timer.elapsed());
            if(!result.errors.isEmpty()) {
                QFile log(Cam::dataDirectory()+"/operations.log");
                if(log.open(QIODevice::Append|QIODevice::Text)) log.write((QDateTime::currentDateTime().toString(Qt::ISODate)+" scan skipped "+QString::number(result.skipped)+"\n"+result.errors.join('\n')+"\n").toUtf8());
                for(int i=0;i<qMin(20,int(result.errors.size()));++i) std::fprintf(stderr,"%s\n",qUtf8Printable(result.errors[i]));
            }
            return 0;
        } catch(const std::exception& e) {std::fprintf(stderr,"scan failed: %s\n",e.what()); return 1;}
    }
    try {
        MainWindow window(parser.value("source")); window.show();
        if(parser.isSet("screenshot")) {
            const QString target=parser.value("screenshot");
            QTimer::singleShot(1200,&window,[&window,target]{window.grab().save(target); QCoreApplication::quit();});
        }
        if(parser.isSet("smoke-test")) QTimer::singleShot(1200,&app,&QApplication::quit);
        return app.exec();
    } catch(const std::exception& e) {QMessageBox::critical(nullptr,QCoreApplication::translate("main","启动失败"),QString::fromUtf8(e.what())); return 1;}
}
