/*
 * Copyright 2026, ablyss supermusicthingy@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QWidget>

// Linux counterpart of the Haiku build's BackgroundUpdateChecker(): shortly
// after launch, fetch VERSION from GitHub and, if it's newer than this build,
// tell the user. A freedesktop desktop notification goes out first; when no
// notification server takes it (notifications disabled, plasmashell not
// running, a bare compositor) the app shows its own "Update Available"
// dialog instead, so the update is never silently missed.
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QWidget* dialogParent);

    void checkLater(int delayMs = 5000);

private:
    void check();
    void notifyUpdate(const QString& remoteVersion);
    void showDialog(const QString& text);

    QNetworkAccessManager m_nam;
    QPointer<QWidget> m_dialogParent;
};
