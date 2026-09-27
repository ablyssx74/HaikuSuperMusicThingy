/*
 * Copyright 2026, Kris Beazley supermusicthingy@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

#include <QString>
#include <array>

// Mirrors the Haiku build's `cfg` globals (see save_config()/load_config() in
// haiku-supermusicthingy.cpp), but persisted with QSettings under
// ~/.config/SuperMusicThingy/config.ini instead of B_USER_SETTINGS_DIRECTORY.
struct AppConfig {
    double currentVolume = 75.0;
    QString quality = "128k"; // Auto, 320k, 256k, 128k, 64k, 32k
    bool showNotifications = false;
    bool sysTrayEnabled = true;
    bool autoShuffleStations = false;
    int shuffleStationsIntervalMinutes = 0; // 0 = disabled
    bool shuffleFavsOnly = false;
    int sleepTimerMinutes = 0; // 0 = disabled

    bool eqEnabled = true;
    std::array<float, 15> eqBands{};
    float limitInputGainDb = 0.0f;
    float limitThresholdDb = 0.0f;
    float limitReleaseMs = 100.0f;
};

class ConfigManager {
public:
    ConfigManager();

    AppConfig& config() { return m_config; }
    const AppConfig& config() const { return m_config; }

    void load();
    void save();

private:
    AppConfig m_config;
};
