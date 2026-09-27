/*
 * Copyright 2026, ablyss supermusicthingy@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>

struct mpv_handle;
struct mpv_event;

// Thin Qt wrapper around libmpv, replacing the Haiku build's direct
// mpv_handle + BLooper-thread event pump with a Qt event-loop friendly one.
class MpvPlayer : public QObject {
    Q_OBJECT
public:
    explicit MpvPlayer(QObject* parent = nullptr);
    ~MpvPlayer() override;

    void play(const QString& url);
    void stop();
    void togglePause();
    void setPaused(bool paused);
    bool isPaused() const { return m_paused; }

    void setVolume(double volume); // 0-100
    double volume() const { return m_volume; }
    void fadeVolumeTo(double target, int durationMs);

    void toggleMute();
    bool isMuted() const { return m_muted; }

    void setAudioFilterChain(const QString& af);

signals:
    void mediaTitleChanged(const QString& title);
    void pausedChanged(bool paused);
    void mutedChanged(bool muted);
    void volumeChanged(double volume);
    void playbackStarted();
    void playbackStopped();
    void bounceLevel(double level); // 0..1 amplitude, for simple UI meters

private slots:
    void processMpvEvents();
    void onFadeTick();

private:
    static void wakeupTrampoline(void* ctx);
    void handleEvent(mpv_event* event);

    mpv_handle* m_mpv = nullptr;
    bool m_paused = false;
    bool m_muted = false;
    double m_volume = 75.0;

    QTimer* m_fadeTimer = nullptr;
    QElapsedTimer m_fadeClock;
    double m_fadeStart = 0.0;
    double m_fadeTarget = 0.0;
    int m_fadeDurationMs = 0;
};
