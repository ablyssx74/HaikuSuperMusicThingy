/*
 * Copyright 2026, ablyss supermusicthingy@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "ConfigManager.h"
#include "Channel.h"

#include <QSettings>

namespace {
QSettings makeSettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, "SuperMusicThingy", "config");
}
} // namespace

ConfigManager::ConfigManager()
{
    for (int i = 0; i < 15; ++i)
        m_config.eqBands[i] = kPresetRock[i];
    load();
}

void ConfigManager::load()
{
    QSettings s = makeSettings();

    m_config.currentVolume = s.value("currentVolume", m_config.currentVolume).toDouble();
    m_config.quality = s.value("quality", m_config.quality).toString();
    m_config.showNotifications = s.value("showNotifications", m_config.showNotifications).toBool();
    m_config.showUpdateNotifications = s.value("showUpdateNotifications", m_config.showUpdateNotifications).toBool();
    m_config.sysTrayEnabled = s.value("sysTrayEnabled", m_config.sysTrayEnabled).toBool();
    m_config.autoShuffleStations = s.value("autoShuffleStations", m_config.autoShuffleStations).toBool();
    m_config.shuffleStationsIntervalMinutes = s.value("shuffleStationsIntervalMinutes", m_config.shuffleStationsIntervalMinutes).toInt();
    m_config.shuffleFavsOnly = s.value("shuffleFavsOnly", m_config.shuffleFavsOnly).toBool();
    m_config.sleepTimerMinutes = s.value("sleepTimerMinutes", m_config.sleepTimerMinutes).toInt();

    m_config.eqEnabled = s.value("eqEnabled", m_config.eqEnabled).toBool();
    QVariantList eqBands = s.value("eqBands").toList();
    for (int i = 0; i < 15 && i < eqBands.size(); ++i)
        m_config.eqBands[i] = eqBands[i].toFloat();
    m_config.limitInputGainDb = s.value("limitInputGainDb", m_config.limitInputGainDb).toFloat();
    m_config.limitThresholdDb = s.value("limitThresholdDb", m_config.limitThresholdDb).toFloat();
    m_config.limitReleaseMs = s.value("limitReleaseMs", m_config.limitReleaseMs).toFloat();
}

void ConfigManager::save()
{
    QSettings s = makeSettings();

    s.setValue("currentVolume", m_config.currentVolume);
    s.setValue("quality", m_config.quality);
    s.setValue("showNotifications", m_config.showNotifications);
    s.setValue("showUpdateNotifications", m_config.showUpdateNotifications);
    s.setValue("sysTrayEnabled", m_config.sysTrayEnabled);
    s.setValue("autoShuffleStations", m_config.autoShuffleStations);
    s.setValue("shuffleStationsIntervalMinutes", m_config.shuffleStationsIntervalMinutes);
    s.setValue("shuffleFavsOnly", m_config.shuffleFavsOnly);
    s.setValue("sleepTimerMinutes", m_config.sleepTimerMinutes);

    s.setValue("eqEnabled", m_config.eqEnabled);
    QVariantList eqBands;
    for (float band : m_config.eqBands)
        eqBands.append(band);
    s.setValue("eqBands", eqBands);
    s.setValue("limitInputGainDb", m_config.limitInputGainDb);
    s.setValue("limitThresholdDb", m_config.limitThresholdDb);
    s.setValue("limitReleaseMs", m_config.limitReleaseMs);

    s.sync();
}
