#include "window.h"
#include <cstdio>

int main(int argc,char* argv[]) {
    QApplication app(argc,argv); app.setApplicationName("TeslaCamManager"); app.setOrganizationName("LocalTools"); app.setApplicationVersion("0.1.0"); app.setStyle("Fusion");
    QPalette palette;
    palette.setColor(QPalette::Window,QColor("#f4f7fa")); palette.setColor(QPalette::WindowText,QColor("#243447"));
    palette.setColor(QPalette::Base,Qt::white); palette.setColor(QPalette::AlternateBase,QColor("#f0f5f8"));
    palette.setColor(QPalette::Text,QColor("#243447")); palette.setColor(QPalette::Button,Qt::white); palette.setColor(QPalette::ButtonText,QColor("#243447"));
    palette.setColor(QPalette::Highlight,QColor("#147d72")); palette.setColor(QPalette::HighlightedText,Qt::white);
    palette.setColor(QPalette::ToolTipBase,Qt::white); palette.setColor(QPalette::ToolTipText,QColor("#243447"));
    palette.setColor(QPalette::Disabled,QPalette::Text,QColor("#93a2af")); palette.setColor(QPalette::Disabled,QPalette::ButtonText,QColor("#93a2af"));
    app.setPalette(palette);
    QCommandLineParser parser; parser.setApplicationDescription("特斯拉影像管家：本地录像索引与事件管理"); parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"source","选择初始录像目录（不会自动扫描）","path"});
    parser.addOption({"smoke-test","启动窗口并自动退出，用于检查运行依赖"}); parser.process(app);
    try {
        MainWindow window(parser.value("source")); window.show();
        if(parser.isSet("smoke-test")) QTimer::singleShot(1200,&app,&QApplication::quit);
        return app.exec();
    } catch(const std::exception& e) {QMessageBox::critical(nullptr,"启动失败",QString::fromUtf8(e.what())); return 1;}
}
