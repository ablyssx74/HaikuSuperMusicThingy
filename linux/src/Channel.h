/*
 * Copyright 2026, Kris Beazley supermusicthingy@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

#include <QSet>
#include <QString>

struct Channel {
    QString title;
    QString id;
    QString desc;
    QString listeners;
    QString largeImage;
    QString image;
    QSet<QString> supportedBitrates;
};

constexpr const char* kSomaFmBaseUrl = "https://somafm.com/";

inline QString qualityUrlForChannel(const Channel& ch, const QString& quality)
{
    const QString base = QString::fromLatin1(kSomaFmBaseUrl);
    if (ch.supportedBitrates.contains(quality)) {
        if (quality == QStringLiteral("320k")) return base + ch.id + "320.pls";
        if (quality == QStringLiteral("256k")) return base + ch.id + "256.pls";
        if (quality == QStringLiteral("64k"))  return base + ch.id + "64.pls";
        if (quality == QStringLiteral("32k"))  return base + ch.id + "32.pls";
    }
    return base + ch.id + ".pls";
}

// Standard mbeq_1197 15-band frequency centers, matching the Haiku build's equalizer.
constexpr float kEqFrequencies[15] = {
    50, 100, 156, 220, 311, 440, 622, 880,
    1250, 1750, 2500, 3500, 5000, 10000, 20000
};

constexpr float kPresetRock[15] = {
    4.0f, 3.5f, 3.0f, 2.5f, 2.0f, 1.0f, -1.0f, -1.0f,
    0.0f, 1.0f, 1.5f, 2.0f, 2.5f, 3.5f, 4.0f
};
constexpr float kPresetJazz[15] = {
    3.0f, 2.5f, 2.0f, 1.5f, 1.0f, 2.0f, -1.0f, -1.0f,
    -0.5f, 0.0f, 0.5f, 1.0f, 1.5f, 2.5f, 3.0f
};
constexpr float kPresetBass[15] = {
    11.0f, 9.0f, 4.0f, 2.0f, 1.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 3.0f, 4.0f, 7.0f, 9.0f
};
constexpr float kPresetFlat[15] = {
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
};
