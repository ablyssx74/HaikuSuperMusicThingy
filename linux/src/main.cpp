/*
 * Copyright 2026, ablyss supermusicthingy@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "MainWindow.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("HaikuSuperMusicThingy");
    app.setOrganizationName("SuperMusicThingy");
    app.setWindowIcon(QIcon(":/icons/icon.svg"));
    app.setQuitOnLastWindowClosed(false);

    MainWindow window;
    window.show();

    return app.exec();
}
