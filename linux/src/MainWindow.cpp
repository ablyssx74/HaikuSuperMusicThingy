/*
 * Copyright 2026, Kris Beazley supermusicthingy@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "MainWindow.h"
#include "MpvPlayer.h"
#include "StationManager.h"
#include "EqualizerWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QListWidget>
#include <QLabel>
#include <QSlider>
#include <QDial>
#include <QToolButton>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QCloseEvent>
#include <QMessageBox>
#include <QStyle>
#include <QApplication>
#include <QRandomGenerator>
#include <QFont>
#include <QIcon>
#include <QSize>

namespace {
const QSize kStationIconSize(32, 32);
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_player(new MpvPlayer(this))
    , m_stations(new StationManager(this))
    , m_eqWidget(nullptr)
{
    setWindowTitle(tr("HaikuSuperMusicThingy"));
    resize(760, 560);

    buildUi();
    buildTrayIcon();

    connect(m_stations, &StationManager::channelsReady, this, &MainWindow::onChannelsReady);
    connect(m_stations, &StationManager::fetchError, this, &MainWindow::onFetchError);
    connect(m_stations, &StationManager::imageReady, this, &MainWindow::onImageReady);

    connect(m_player, &MpvPlayer::mediaTitleChanged, this, &MainWindow::onMediaTitleChanged);
    connect(m_player, &MpvPlayer::pausedChanged, this, &MainWindow::onPausedChanged);
    connect(m_player, &MpvPlayer::mutedChanged, this, &MainWindow::onMutedChanged);

    connect(m_eqWidget, &EqualizerWidget::filterChainChanged, this, &MainWindow::onFilterChainChanged);

    applyConfigToUi();
    m_player->setVolume(m_configManager.config().currentVolume);
    m_player->setAudioFilterChain(m_eqWidget->buildFilterChain());

    m_sleepTimer = new QTimer(this);
    m_sleepTimer->setSingleShot(true);
    connect(m_sleepTimer, &QTimer::timeout, this, &MainWindow::onSleepTimerFired);

    m_shuffleStationsTimer = new QTimer(this);
    connect(m_shuffleStationsTimer, &QTimer::timeout, this, &MainWindow::onShuffleStationsTimerFired);

    onSleepTimerChanged(m_sleepCombo->currentIndex());
    onShuffleStationsTimerChanged(m_shuffleStationsCombo->currentIndex());

    m_stations->fetchChannels();
}

MainWindow::~MainWindow()
{
    saveUiToConfig();
    m_configManager.save();
}

void MainWindow::buildUi()
{
    m_eqWidget = new EqualizerWidget(this);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(buildPlayerTab(), tr("Player"));
    tabs->addTab(buildStationsTab(), tr("Stations"));
    tabs->addTab(buildFavoritesTab(), tr("Favorites"));
    tabs->addTab(m_eqWidget, tr("Equalizer"));
    tabs->addTab(buildConfigTab(), tr("Config"));
    tabs->addTab(buildAboutTab(), tr("About"));
    setCentralWidget(tabs);
}

QWidget* MainWindow::buildPlayerTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    m_albumArtLabel = new QLabel(page);
    m_albumArtLabel->setFixedSize(200, 200);
    m_albumArtLabel->setAlignment(Qt::AlignCenter);
    m_albumArtLabel->setStyleSheet("background-color: rgba(0,0,0,40); border-radius: 4px;");
    m_albumArtLabel->setText(tr("No Art"));

    auto* artRow = new QHBoxLayout();
    artRow->addStretch();
    artRow->addWidget(m_albumArtLabel);
    artRow->addStretch();
    layout->addLayout(artRow);

    m_stationNameLabel = new QLabel(tr("No Station Selected"), page);
    m_stationNameLabel->setAlignment(Qt::AlignCenter);
    QFont nameFont = m_stationNameLabel->font();
    nameFont.setPointSize(nameFont.pointSize() + 4);
    nameFont.setBold(true);
    m_stationNameLabel->setFont(nameFont);
    layout->addWidget(m_stationNameLabel);

    m_descLabel = new QLabel(page);
    m_descLabel->setAlignment(Qt::AlignCenter);
    m_descLabel->setWordWrap(true);
    layout->addWidget(m_descLabel);

    m_songLabel = new QLabel(tr("Not Playing"), page);
    m_songLabel->setAlignment(Qt::AlignCenter);
    m_songLabel->setWordWrap(true);
    layout->addWidget(m_songLabel);

    auto* transport = new QHBoxLayout();
    QStyle* style = QApplication::style();

    m_playButton = new QToolButton(page);
    m_playButton->setIcon(style->standardIcon(QStyle::SP_MediaPlay));
    m_playButton->setToolTip(tr("Play"));

    m_pauseButton = new QToolButton(page);
    m_pauseButton->setIcon(style->standardIcon(QStyle::SP_MediaPause));
    m_pauseButton->setToolTip(tr("Pause"));

    m_stopButton = new QToolButton(page);
    m_stopButton->setIcon(style->standardIcon(QStyle::SP_MediaStop));
    m_stopButton->setToolTip(tr("Stop"));

    m_muteButton = new QToolButton(page);
    m_muteButton->setIcon(style->standardIcon(QStyle::SP_MediaVolume));
    m_muteButton->setToolTip(tr("Mute"));

    auto* shuffleButton = new QPushButton(tr("Shuffle"), page);
    connect(shuffleButton, &QPushButton::clicked, this, &MainWindow::onShuffleClicked);

    transport->addStretch();
    transport->addWidget(m_playButton);
    transport->addWidget(m_pauseButton);
    transport->addWidget(m_stopButton);
    transport->addWidget(m_muteButton);
    transport->addWidget(shuffleButton);
    transport->addStretch();
    layout->addLayout(transport);

    auto* volumeColumn = new QVBoxLayout();
    volumeColumn->addWidget(new QLabel(tr("Volume"), page), 0, Qt::AlignHCenter);
    m_volumeDial = new QDial(page);
    m_volumeDial->setRange(0, 100);
    m_volumeDial->setValue(75);
    m_volumeDial->setNotchesVisible(true);
    m_volumeDial->setFixedSize(80, 80);
    volumeColumn->addWidget(m_volumeDial, 0, Qt::AlignHCenter);
    m_volumeValueLabel = new QLabel("75%", page);
    m_volumeValueLabel->setAlignment(Qt::AlignCenter);
    volumeColumn->addWidget(m_volumeValueLabel);

    auto* volumeRow = new QHBoxLayout();
    volumeRow->addStretch();
    volumeRow->addLayout(volumeColumn);
    volumeRow->addStretch();
    layout->addLayout(volumeRow);

    layout->addStretch();

    connect(m_playButton, &QToolButton::clicked, this, &MainWindow::onPlayClicked);
    connect(m_pauseButton, &QToolButton::clicked, this, &MainWindow::onPauseClicked);
    connect(m_stopButton, &QToolButton::clicked, this, &MainWindow::onStopClicked);
    connect(m_muteButton, &QToolButton::clicked, this, &MainWindow::onMuteClicked);
    connect(m_volumeDial, &QDial::valueChanged, this, &MainWindow::onVolumeDialMoved);

    return page;
}

QWidget* MainWindow::buildStationsTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    m_stationList = new QListWidget(page);
    m_stationList->setIconSize(kStationIconSize);
    layout->addWidget(m_stationList);

    auto* buttonRow = new QHBoxLayout();
    auto* refreshButton = new QPushButton(tr("Refresh"), page);
    auto* shuffleButton = new QPushButton(tr("Play Random Station"), page);
    buttonRow->addWidget(refreshButton);
    buttonRow->addWidget(shuffleButton);
    layout->addLayout(buttonRow);

    connect(m_stationList, &QListWidget::itemActivated, this, &MainWindow::onStationActivated);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::onRefreshStationsClicked);
    connect(shuffleButton, &QPushButton::clicked, this, &MainWindow::onShuffleClicked);

    return page;
}

QWidget* MainWindow::buildFavoritesTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    m_favoritesList = new QListWidget(page);
    m_favoritesList->setIconSize(kStationIconSize);
    layout->addWidget(m_favoritesList);

    auto* buttonRow = new QHBoxLayout();
    auto* addButton = new QPushButton(tr("Add Current Station"), page);
    auto* removeButton = new QPushButton(tr("Remove Selected"), page);
    auto* playRandomButton = new QPushButton(tr("Play Random Favorite"), page);
    buttonRow->addWidget(addButton);
    buttonRow->addWidget(removeButton);
    buttonRow->addWidget(playRandomButton);
    layout->addLayout(buttonRow);

    connect(m_favoritesList, &QListWidget::itemActivated, this, &MainWindow::onFavoriteActivated);
    connect(addButton, &QPushButton::clicked, this, &MainWindow::onAddFavoriteClicked);
    connect(removeButton, &QPushButton::clicked, this, &MainWindow::onRemoveFavoriteClicked);
    connect(playRandomButton, &QPushButton::clicked, this, &MainWindow::onPlayRandomFavoriteClicked);

    return page;
}

QWidget* MainWindow::buildConfigTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QFormLayout(page);

    m_notifyCheck = new QCheckBox(tr("Show song-change notifications"), page);
    layout->addRow(m_notifyCheck);

    m_trayCheck = new QCheckBox(tr("Enable system tray icon"), page);
    layout->addRow(m_trayCheck);

    m_qualityCombo = new QComboBox(page);
    m_qualityCombo->addItem(tr("Auto (Default)"), "128k");
    m_qualityCombo->addItem(tr("320 kbps"), "320k");
    m_qualityCombo->addItem(tr("256 kbps"), "256k");
    m_qualityCombo->addItem(tr("64 kbps"), "64k");
    m_qualityCombo->addItem(tr("32 kbps"), "32k");
    layout->addRow(tr("Stream Quality"), m_qualityCombo);

    m_shuffleFavsOnlyCheck = new QCheckBox(tr("Shuffle favorites only"), page);
    layout->addRow(m_shuffleFavsOnlyCheck);

    m_sleepCombo = new QComboBox(page);
    m_sleepCombo->addItem(tr("Disabled"), 0);
    m_sleepCombo->addItem(tr("15 Minutes"), 15);
    m_sleepCombo->addItem(tr("30 Minutes"), 30);
    m_sleepCombo->addItem(tr("1 Hour"), 60);
    m_sleepCombo->addItem(tr("3 Hours"), 180);
    m_sleepCombo->addItem(tr("6 Hours"), 360);
    m_sleepCombo->addItem(tr("8 Hours"), 480);
    layout->addRow(tr("Sleep Timer"), m_sleepCombo);

    m_shuffleStationsCombo = new QComboBox(page);
    m_shuffleStationsCombo->addItem(tr("Disabled"), 0);
    m_shuffleStationsCombo->addItem(tr("1 Minute"), 1);
    m_shuffleStationsCombo->addItem(tr("5 Minutes"), 5);
    m_shuffleStationsCombo->addItem(tr("15 Minutes"), 15);
    m_shuffleStationsCombo->addItem(tr("30 Minutes"), 30);
    m_shuffleStationsCombo->addItem(tr("1 Hour"), 60);
    m_shuffleStationsCombo->addItem(tr("3 Hours"), 180);
    layout->addRow(tr("Auto-Shuffle Stations"), m_shuffleStationsCombo);

    connect(m_trayCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        m_trayIcon->setVisible(enabled);
    });
    connect(m_sleepCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onSleepTimerChanged);
    connect(m_shuffleStationsCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onShuffleStationsTimerChanged);

    return page;
}

QWidget* MainWindow::buildAboutTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* label = new QLabel(page);
    label->setTextFormat(Qt::RichText);
    label->setOpenExternalLinks(true);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setText(tr(
        "<h2>HaikuSuperMusicThingy</h2>"
        "<p>A free streaming media client for <a href=\"https://somafm.com/\">SomaFM</a>.</p>"
        "<p>Linux port (Qt6 / libmpv) of the original Haiku OS application.</p>"
        "<p>Playback: libmpv &middot; Networking: Qt Network &middot; UI: Qt Widgets</p>"
        "<p><a href=\"https://github.com/ablyssx74/HaikuSuperMusicThingy\">"
        "github.com/ablyssx74/HaikuSuperMusicThingy</a></p>"
        "<p>Copyright 2026, Kris Beazley. Distributed under the MIT license.</p>"));

    layout->addStretch();
    layout->addWidget(label);
    layout->addStretch();

    return page;
}

void MainWindow::buildTrayIcon()
{
    m_trayMenu = new QMenu(this);

    m_playPauseTrayAction = m_trayMenu->addAction(tr("Play/Pause"));
    connect(m_playPauseTrayAction, &QAction::triggered, this, [this]() { m_player->togglePause(); });

    QAction* stopAction = m_trayMenu->addAction(tr("Stop"));
    connect(stopAction, &QAction::triggered, this, &MainWindow::onStopClicked);

    QAction* shuffleAction = m_trayMenu->addAction(tr("Shuffle"));
    connect(shuffleAction, &QAction::triggered, this, &MainWindow::onShuffleClicked);

    m_trayMenu->addSeparator();

    QAction* showAction = m_trayMenu->addAction(tr("Show/Hide Window"));
    connect(showAction, &QAction::triggered, this, [this]() {
        setVisible(!isVisible());
        if (isVisible()) {
            activateWindow();
            raise();
        }
    });

    QAction* quitAction = m_trayMenu->addAction(tr("Quit"));
    connect(quitAction, &QAction::triggered, this, [this]() {
        m_quitting = true;
        close();
    });

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(QApplication::style()->standardIcon(QStyle::SP_MediaVolume));
    m_trayIcon->setToolTip(tr("HaikuSuperMusicThingy"));
    m_trayIcon->setContextMenu(m_trayMenu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        onTrayActivated(static_cast<int>(reason));
    });
}

void MainWindow::applyConfigToUi()
{
    const AppConfig& cfg = m_configManager.config();

    m_volumeDial->setValue(static_cast<int>(cfg.currentVolume));
    m_volumeValueLabel->setText(QString("%1%").arg(static_cast<int>(cfg.currentVolume)));
    m_notifyCheck->setChecked(cfg.showNotifications);
    m_trayCheck->setChecked(cfg.sysTrayEnabled);
    m_qualityCombo->setCurrentIndex(m_qualityCombo->findData(cfg.quality));
    m_shuffleFavsOnlyCheck->setChecked(cfg.shuffleFavsOnly);
    m_sleepCombo->setCurrentIndex(m_sleepCombo->findData(cfg.sleepTimerMinutes) >= 0
                                       ? m_sleepCombo->findData(cfg.sleepTimerMinutes)
                                       : 0);
    m_shuffleStationsCombo->setCurrentIndex(
        m_shuffleStationsCombo->findData(cfg.shuffleStationsIntervalMinutes) >= 0
            ? m_shuffleStationsCombo->findData(cfg.shuffleStationsIntervalMinutes)
            : 0);

    m_trayIcon->setVisible(cfg.sysTrayEnabled);
    m_eqWidget->loadFromConfig(cfg);
}

void MainWindow::saveUiToConfig()
{
    AppConfig& cfg = m_configManager.config();
    cfg.currentVolume = m_volumeDial->value();
    cfg.showNotifications = m_notifyCheck->isChecked();
    cfg.sysTrayEnabled = m_trayCheck->isChecked();
    cfg.quality = m_qualityCombo->currentData().toString();
    cfg.shuffleFavsOnly = m_shuffleFavsOnlyCheck->isChecked();
    cfg.sleepTimerMinutes = m_sleepCombo->currentData().toInt();
    cfg.shuffleStationsIntervalMinutes = m_shuffleStationsCombo->currentData().toInt();
    m_eqWidget->saveToConfig(cfg);
}

const Channel* MainWindow::channelById(const QString& id) const
{
    for (const auto& ch : m_channels) {
        if (ch.id == id)
            return &ch;
    }
    return nullptr;
}

void MainWindow::onChannelsReady(const QVector<Channel>& channels)
{
    m_channels = channels;

    m_stationList->clear();
    for (const auto& ch : m_channels) {
        auto* item = new QListWidgetItem(ch.title, m_stationList);
        item->setData(Qt::UserRole, ch.id);
        item->setToolTip(ch.desc);
        if (m_artCache.contains(ch.image))
            item->setIcon(QIcon(m_artCache[ch.image]));
        else
            requestStationIcon(ch);
    }

    refreshFavoritesList();
}

void MainWindow::onFetchError(const QString& message)
{
    m_songLabel->setText(tr("Could not load station list: %1").arg(message));
}

void MainWindow::onImageReady(const QString& url, const QPixmap& pixmap)
{
    m_artCache[url] = pixmap;

    if (!m_currentChannelId.isEmpty()) {
        const Channel* current = channelById(m_currentChannelId);
        if (current && current->largeImage == url) {
            m_albumArtLabel->setPixmap(pixmap.scaled(m_albumArtLabel->size(),
                                                      Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }

    for (const auto& ch : m_channels) {
        if (ch.image != url)
            continue;
        applyStationIcon(m_stationList, ch.id, pixmap);
        applyStationIcon(m_favoritesList, ch.id, pixmap);
    }
}

void MainWindow::requestStationIcon(const Channel& channel)
{
    if (!channel.image.isEmpty())
        m_stations->fetchImage(channel.image);
}

void MainWindow::applyStationIcon(QListWidget* list, const QString& channelId, const QPixmap& pixmap)
{
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem* item = list->item(i);
        if (item->data(Qt::UserRole).toString() == channelId)
            item->setIcon(QIcon(pixmap));
    }
}

void MainWindow::refreshFavoritesList()
{
    m_favoritesList->clear();
    for (const QString& id : m_favorites.favoriteIds()) {
        const Channel* ch = channelById(id);
        auto* item = new QListWidgetItem(ch ? ch->title : id, m_favoritesList);
        item->setData(Qt::UserRole, id);
        if (ch) {
            if (m_artCache.contains(ch->image))
                item->setIcon(QIcon(m_artCache[ch->image]));
            else
                requestStationIcon(*ch);
        }
    }
}

void MainWindow::playChannel(const Channel& channel)
{
    m_currentChannelId = channel.id;
    m_stationNameLabel->setText(channel.title);
    m_descLabel->setText(channel.desc);
    m_songLabel->setText(tr("Buffering..."));

    if (m_artCache.contains(channel.largeImage)) {
        m_albumArtLabel->setPixmap(m_artCache[channel.largeImage].scaled(
            m_albumArtLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        m_albumArtLabel->setPixmap(QPixmap());
        m_albumArtLabel->setText(tr("No Art"));
        if (!channel.largeImage.isEmpty())
            m_stations->fetchImage(channel.largeImage);
    }

    m_player->fadeVolumeTo(0, 250);
    QString url = qualityUrlForChannel(channel, m_qualityCombo->currentData().toString());
    m_player->play(url);
    m_player->fadeVolumeTo(m_volumeDial->value(), 500);
}

void MainWindow::onStationActivated(QListWidgetItem* item)
{
    const Channel* ch = channelById(item->data(Qt::UserRole).toString());
    if (ch)
        playChannel(*ch);
}

void MainWindow::onFavoriteActivated(QListWidgetItem* item)
{
    const Channel* ch = channelById(item->data(Qt::UserRole).toString());
    if (ch)
        playChannel(*ch);
}

void MainWindow::onPlayClicked() { m_player->setPaused(false); }
void MainWindow::onPauseClicked() { m_player->setPaused(true); }
void MainWindow::onStopClicked()
{
    m_player->stop();
    m_songLabel->setText(tr("Stopped"));
}

void MainWindow::onMuteClicked() { m_player->toggleMute(); }

void MainWindow::onShuffleClicked()
{
    if (m_channels.isEmpty())
        return;

    if (m_shuffleFavsOnlyCheck->isChecked() && !m_favorites.favoriteIds().isEmpty()) {
        const Channel* ch = channelById(m_favorites.randomFavoriteId());
        if (ch) {
            playChannel(*ch);
            return;
        }
    }

    int index = QRandomGenerator::global()->bounded(m_channels.size());
    playChannel(m_channels.at(index));
}

void MainWindow::onRefreshStationsClicked()
{
    m_stations->fetchChannels();
}

void MainWindow::onAddFavoriteClicked()
{
    if (m_currentChannelId.isEmpty())
        return;
    m_favorites.addFavorite(m_currentChannelId);
    refreshFavoritesList();
}

void MainWindow::onRemoveFavoriteClicked()
{
    QListWidgetItem* item = m_favoritesList->currentItem();
    if (!item)
        return;
    m_favorites.removeFavorite(item->data(Qt::UserRole).toString());
    refreshFavoritesList();
}

void MainWindow::onPlayRandomFavoriteClicked()
{
    const Channel* ch = channelById(m_favorites.randomFavoriteId());
    if (ch)
        playChannel(*ch);
}

void MainWindow::onVolumeDialMoved(int value)
{
    m_player->setVolume(value);
    m_volumeValueLabel->setText(QString("%1%").arg(value));
}

void MainWindow::onMediaTitleChanged(const QString& title)
{
    m_songLabel->setText(title);
    if (m_notifyCheck->isChecked())
        showNotification(m_stationNameLabel->text(), title);
}

void MainWindow::onPausedChanged(bool paused)
{
    m_playButton->setEnabled(paused);
    m_pauseButton->setEnabled(!paused);
}

void MainWindow::onMutedChanged(bool muted)
{
    QStyle* style = QApplication::style();
    m_muteButton->setIcon(style->standardIcon(muted ? QStyle::SP_MediaVolumeMuted : QStyle::SP_MediaVolume));
}

void MainWindow::onFilterChainChanged(const QString& af)
{
    m_player->setAudioFilterChain(af);
}

void MainWindow::onSleepTimerChanged(int index)
{
    int minutes = m_sleepCombo->itemData(index).toInt();
    if (minutes <= 0) {
        m_sleepTimer->stop();
    } else {
        m_sleepTimer->start(minutes * 60 * 1000);
    }
}

void MainWindow::onShuffleStationsTimerChanged(int index)
{
    int minutes = m_shuffleStationsCombo->itemData(index).toInt();
    if (minutes <= 0) {
        m_shuffleStationsTimer->stop();
    } else {
        m_shuffleStationsTimer->start(minutes * 60 * 1000);
    }
}

void MainWindow::onSleepTimerFired()
{
    m_player->stop();
    m_songLabel->setText(tr("Sleep timer expired - playback stopped"));
    showNotification(tr("Sleep Timer"), tr("Playback stopped."));
    m_sleepCombo->setCurrentIndex(0);
}

void MainWindow::onShuffleStationsTimerFired()
{
    onShuffleClicked();
}

void MainWindow::onTrayActivated(int reason)
{
    if (reason == QSystemTrayIcon::Trigger) {
        setVisible(!isVisible());
        if (isVisible()) {
            activateWindow();
            raise();
        }
    }
}

void MainWindow::showNotification(const QString& title, const QString& body)
{
    if (m_trayIcon->isVisible())
        m_trayIcon->showMessage(title, body, QSystemTrayIcon::Information, 4000);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!m_quitting && m_trayCheck->isChecked() && m_trayIcon->isVisible()) {
        hide();
        event->ignore();
        return;
    }
    saveUiToConfig();
    m_configManager.save();
    event->accept();
    qApp->quit();
}
