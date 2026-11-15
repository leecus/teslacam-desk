#include "window.h"
#include "theme.h"
#include "i18n.h"
#include <cstdio>

int main(int argc,char* argv[]) {
    QApplication app(argc,argv); app.setApplicationName("TeslaCamManager"); app.setOrganizationName("LocalTools"); app.setApplicationVersion("0.1.0"); app.setStyle("Fusion");
    QCommandLineParser parser; parser.setApplicationDescription(QCoreApplication::translate("main","特斯拉影像管家：本地录像索引与事件管理")); parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"source",QCoreApplication::translate("main","选择初始录像目录（不会自动扫描）"),"path"});
    parser.addOption({"theme",QCoreApplication::translate("main","启动时使用 light 或 dark 外观（不写入设置）"),"light|dark"});
    parser.addOption({"lang",QCoreApplication::translate("main","启动时使用 zh 或 en 界面（不写入设置）"),"zh|en"});
    parser.addOption({"screenshot",QCoreApplication::translate("main","启动后把窗口渲染为 PNG 并退出，用于检查界面"),"path"});
    parser.addOption({"smoke-test",QCoreApplication::translate("main","启动窗口并自动退出，用于检查运行依赖")}); parser.process(app);
    I18n::install(parser.isSet("lang")?I18n::parse(parser.value("lang")):I18n::load());
    Theme::apply(parser.isSet("theme")?Theme::parse(parser.value("theme")):Theme::load());
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
