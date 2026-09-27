/*
 * Copyright 2026, ablyss supermusicthingy@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

#include "Channel.h"

#include <QObject>
#include <QVector>
#include <QPixmap>
#include <QNetworkAccessManager>

// Fetches the SomaFM channel list and station/album art over the network,
// replacing the Haiku build's libcurl-based fetch_channels()/download_art().
class StationManager : public QObject {
    Q_OBJECT
public:
    explicit StationManager(QObject* parent = nullptr);

    void fetchChannels();
    void fetchImage(const QString& url); // emits imageReady(url, pixmap)

    const QVector<Channel>& channels() const { return m_channels; }
    const Channel* findById(const QString& id) const;

signals:
    void channelsReady(const QVector<Channel>& channels);
    void fetchError(const QString& message);
    void imageReady(const QString& url, const QPixmap& pixmap);

private:
    QNetworkAccessManager m_net;
    QVector<Channel> m_channels;
};
