/*
 * Copyright 2026, ablyss supermusicthingy@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "UpdateChecker.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDesktopServices>
#include <QMessageBox>
#include <QNetworkReply>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QtGlobal>

#include <cstdio>

namespace {
const char* const kVersionUrl =
    "https://raw.githubusercontent.com/ablyssx74/HaikuSuperMusicThingy/refs/heads/main/VERSION";
const char* const kProjectUrl = "https://github.com/ablyssx74/HaikuSuperMusicThingy";

// "v1.0.12" -> 10012, the same flattening the Haiku build uses (0 if unparsable).
int flattenVersion(const QString& s)
{
    static const QRegularExpression re(R"((\d+)\.(\d+)\.(\d+))");
    const QRegularExpressionMatch m = re.match(s);
    if (!m.hasMatch())
        return 0;
    return m.captured(1).toInt() * 10000 + m.captured(2).toInt() * 100 + m.captured(3).toInt();
}
} // namespace

UpdateChecker::UpdateChecker(QWidget* dialogParent)
    : QObject(dialogParent)
    , m_dialogParent(dialogParent)
{
}

void UpdateChecker::checkLater(int delayMs)
{
    QTimer::singleShot(delayMs, this, &UpdateChecker::check);
}

void UpdateChecker::check()
{
    QNetworkRequest req{QUrl(kVersionUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "SuperMusicThingy/1.0");
    req.setTransferTimeout(10000);
    QNetworkReply* reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QString remote = QString::fromUtf8(reply->readAll()).trimmed();
        if (reply->error() != QNetworkReply::NoError || remote.isEmpty()) {
            fprintf(stderr, "[UPDATE] Update check failed: %s\n", qPrintable(reply->errorString()));
            return;
        }
        if (flattenVersion(remote) > flattenVersion(APP_VERSION))
            notifyUpdate(remote);
        else
            fprintf(stderr, "[UPDATE] Up to date (v%s)\n", APP_VERSION);
    });
}

void UpdateChecker::notifyUpdate(const QString& remoteVersion)
{
    const QString title = tr("Update Available");
    const QString text = tr("A newer version of HaikuSuperMusicThingy is available! (%1)").arg(remoteVersion);

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        fprintf(stderr, "[UPDATE] No D-Bus session bus; showing the update alert\n");
        showDialog(text);
        return;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall("org.freedesktop.Notifications",
        "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify");
    QVariantMap hints;
    hints["desktop-entry"] = QString("haikusupermusicthingy");
    msg << QString("HaikuSuperMusicThingy") << uint(0) << QString("haikusupermusicthingy") << title << text
        << QStringList() << hints << int(-1);

    // Watch the reply: with nothing owning org.freedesktop.Notifications the
    // call fails, and then the dialog says it instead.
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, text](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (!w->isError()) {
            fprintf(stderr, "[UPDATE] Update notification shown\n");
            return;
        }
        fprintf(stderr, "[UPDATE] Update notification not shown (%s); showing an alert\n",
            qPrintable(w->error().message()));
        showDialog(text);
    });
}

void UpdateChecker::showDialog(const QString& text)
{
    QMessageBox box(QMessageBox::Information, tr("Update Available"),
        text + "\n\n" + tr("You are running v%1.").arg(APP_VERSION), QMessageBox::NoButton, m_dialogParent);
    box.addButton(tr("Later"), QMessageBox::RejectRole);
    QPushButton* open = box.addButton(tr("Open GitHub"), QMessageBox::AcceptRole);
    box.setDefaultButton(open);
    box.exec();
    if (box.clickedButton() == open)
        QDesktopServices::openUrl(QUrl(kProjectUrl));
}
