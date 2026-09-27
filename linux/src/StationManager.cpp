/*
 * Copyright 2026, Kris Beazley supermusicthingy@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "StationManager.h"

#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QUrl>

StationManager::StationManager(QObject* parent)
    : QObject(parent)
{
}

const Channel* StationManager::findById(const QString& id) const
{
    for (const auto& ch : m_channels) {
        if (ch.id == id)
            return &ch;
    }
    return nullptr;
}

void StationManager::fetchChannels()
{
    QUrl url(QStringLiteral("%1channels.json").arg(kSomaFmBaseUrl));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("SuperMusicThingy/1.0"));

    QNetworkReply* reply = m_net.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit fetchError(reply->errorString());
            return;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            emit fetchError(parseError.errorString());
            return;
        }

        QVector<Channel> channels;
        const QJsonArray channelArray = doc.object().value("channels").toArray();
        for (const QJsonValue& v : channelArray) {
            const QJsonObject obj = v.toObject();
            Channel channel;
            channel.title = obj.value("title").toString();
            channel.id = obj.value("id").toString();
            channel.desc = obj.value("description").toString();
            channel.listeners = obj.value("listeners").toVariant().toString();
            channel.largeImage = obj.value("largeimage").toString();
            channel.image = obj.value("image").toString();

            const QJsonArray playlists = obj.value("playlists").toArray();
            for (const QJsonValue& plVal : playlists) {
                const QString plUrl = plVal.toObject().value("url").toString();
                if (plUrl.contains("320.pls")) channel.supportedBitrates.insert("320k");
                if (plUrl.contains("256.pls")) channel.supportedBitrates.insert("256k");
                if (plUrl.contains("64.pls")) channel.supportedBitrates.insert("64k");
                if (plUrl.contains("32.pls")) channel.supportedBitrates.insert("32k");
            }

            channels.push_back(channel);
        }

        m_channels = channels;
        emit channelsReady(m_channels);
    });
}

void StationManager::fetchImage(const QString& url)
{
    if (url.isEmpty())
        return;

    QUrl imageUrl(url);
    QNetworkRequest request(imageUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("SuperMusicThingy/1.0"));

    QNetworkReply* reply = m_net.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;

        QPixmap pixmap;
        if (pixmap.loadFromData(reply->readAll()))
            emit imageReady(url, pixmap);
    });
}
