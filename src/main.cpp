#include <QApplication>
#include <QFontDatabase>
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("ForgeVM");
    app.setOrganizationName("forgevm");
    app.setApplicationVersion("1.0.0");

    // Use Fusion style as base
    app.setStyle("Fusion");

    MainWindow window;
    window.show();

    return app.exec();
}
