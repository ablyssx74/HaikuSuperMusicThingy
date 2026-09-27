/*
 * Copyright 2026, Kris Beazley supermusicthingy@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

#include "Channel.h"
#include "ConfigManager.h"
#include "FavoritesManager.h"

#include <QMainWindow>
#include <QMap>
#include <QPixmap>
#include <QVector>

class MpvPlayer;
class StationManager;
class EqualizerWidget;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QSlider;
class QToolButton;
class QCheckBox;
class QComboBox;
class QSystemTrayIcon;
class QMenu;
class QAction;
class QTimer;

// The Linux/Qt equivalent of SuperMusicWindow (haiku-supermusicthingy.h/.cpp).
// Scope note: this first pass covers the core player - playback, stations,
// favorites, equalizer, notifications and tray - it does not yet port the
// projectM visualizer window or the custom spectrum-game views.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onChannelsReady(const QVector<Channel>& channels);
    void onFetchError(const QString& message);
    void onImageReady(const QString& url, const QPixmap& pixmap);

    void onStationActivated(QListWidgetItem* item);
    void onFavoriteActivated(QListWidgetItem* item);

    void onPlayClicked();
    void onPauseClicked();
    void onStopClicked();
    void onMuteClicked();
    void onShuffleClicked();
    void onRefreshStationsClicked();

    void onAddFavoriteClicked();
    void onRemoveFavoriteClicked();
    void onPlayRandomFavoriteClicked();

    void onVolumeSliderMoved(int value);
    void onMediaTitleChanged(const QString& title);
    void onPausedChanged(bool paused);
    void onMutedChanged(bool muted);

    void onFilterChainChanged(const QString& af);

    void onSleepTimerChanged(int index);
    void onShuffleStationsTimerChanged(int index);
    void onSleepTimerFired();
    void onShuffleStationsTimerFired();

    void onTrayActivated(int reason);

private:
    void buildUi();
    QWidget* buildPlayerTab();
    QWidget* buildStationsTab();
    QWidget* buildFavoritesTab();
    QWidget* buildConfigTab();
    QWidget* buildAboutTab();
    void buildTrayIcon();

    void playChannel(const Channel& channel);
    const Channel* channelById(const QString& id) const;
    void refreshFavoritesList();
    void showNotification(const QString& title, const QString& body);
    void applyConfigToUi();
    void saveUiToConfig();

    MpvPlayer* m_player;
    StationManager* m_stations;
    ConfigManager m_configManager;
    FavoritesManager m_favorites;
    EqualizerWidget* m_eqWidget;

    QVector<Channel> m_channels;
    QMap<QString, QPixmap> m_artCache;
    QString m_currentChannelId;

    // Player tab
    QLabel* m_albumArtLabel;
    QLabel* m_stationNameLabel;
    QLabel* m_descLabel;
    QLabel* m_songLabel;
    QToolButton* m_playButton;
    QToolButton* m_pauseButton;
    QToolButton* m_stopButton;
    QToolButton* m_muteButton;
    QSlider* m_volumeSlider;

    // Stations tab
    QListWidget* m_stationList;

    // Favorites tab
    QListWidget* m_favoritesList;

    // Config tab
    QCheckBox* m_notifyCheck;
    QCheckBox* m_trayCheck;
    QComboBox* m_qualityCombo;
    QCheckBox* m_shuffleFavsOnlyCheck;
    QComboBox* m_sleepCombo;
    QComboBox* m_shuffleStationsCombo;

    QSystemTrayIcon* m_trayIcon;
    QMenu* m_trayMenu;
    QAction* m_playPauseTrayAction;

    QTimer* m_sleepTimer;
    QTimer* m_shuffleStationsTimer;

    bool m_quitting = false;
};
