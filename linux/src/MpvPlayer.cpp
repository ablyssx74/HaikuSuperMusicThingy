/*
 * Copyright 2026, ablyss supermusicthingy@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "MpvPlayer.h"

#include <mpv/client.h>

#include <QMetaObject>
#include <QByteArray>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <clocale>

MpvPlayer::MpvPlayer(QObject* parent)
    : QObject(parent)
{
    // QApplication calls setlocale(LC_ALL, "") on startup to match the
    // system locale (for input methods, number formatting, etc.), which on
    // any non-English locale leaves LC_NUMERIC using a decimal separator
    // other than '.'. mpv parses/formats floats assuming the C locale and
    // refuses to initialize otherwise, so it must be forced back here.
    setlocale(LC_NUMERIC, "C");

    m_mpv = mpv_create();
    if (!m_mpv) {
        qFatal("Failed to create mpv instance");
    }

    // Let mpv auto-select the audio output (pipewire/pulse/alsa); Haiku's
    // build forces "openal" since that's the only sane choice there.
    mpv_set_option_string(m_mpv, "input-default-bindings", "yes");
    mpv_set_option_string(m_mpv, "terminal", "no");
    mpv_set_option_string(m_mpv, "vid", "no");
    mpv_set_option_string(m_mpv, "video", "no");

    if (mpv_initialize(m_mpv) < 0) {
        qFatal("Failed to initialize mpv");
    }

    mpv_observe_property(m_mpv, 0, "media-title", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "audio-bitrate", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "af-metadata/bouncy", MPV_FORMAT_NODE);

    mpv_set_wakeup_callback(m_mpv, &MpvPlayer::wakeupTrampoline, this);

    setVolume(m_volume);

    m_fadeTimer = new QTimer(this);
    m_fadeTimer->setInterval(30);
    connect(m_fadeTimer, &QTimer::timeout, this, &MpvPlayer::onFadeTick);
}

MpvPlayer::~MpvPlayer()
{
    if (m_mpv) {
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

void MpvPlayer::wakeupTrampoline(void* ctx)
{
    // Called from mpv's internal thread; queue the real handling onto the
    // Qt main thread instead of touching mpv/Qt objects here.
    auto* self = static_cast<MpvPlayer*>(ctx);
    QMetaObject::invokeMethod(self, "processMpvEvents", Qt::QueuedConnection);
}

void MpvPlayer::processMpvEvents()
{
    while (m_mpv) {
        mpv_event* event = mpv_wait_event(m_mpv, 0);
        if (!event || event->event_id == MPV_EVENT_NONE)
            break;
        handleEvent(event);
    }
}

void MpvPlayer::handleEvent(mpv_event* event)
{
    switch (event->event_id) {
    case MPV_EVENT_PROPERTY_CHANGE: {
        auto* prop = static_cast<mpv_event_property*>(event->data);
        if (!prop->data)
            break;
        if (QLatin1String(prop->name) == "media-title" && prop->format == MPV_FORMAT_STRING) {
            QString title = QString::fromUtf8(*static_cast<char**>(prop->data));
            if (!title.startsWith("http"))
                emit mediaTitleChanged(title);
        } else if (QLatin1String(prop->name) == "pause" && prop->format == MPV_FORMAT_FLAG) {
            m_paused = *static_cast<int*>(prop->data) != 0;
            emit pausedChanged(m_paused);
        } else if (QLatin1String(prop->name) == "mute" && prop->format == MPV_FORMAT_FLAG) {
            m_muted = *static_cast<int*>(prop->data) != 0;
            emit mutedChanged(m_muted);
        } else if (QLatin1String(prop->name) == "volume" && prop->format == MPV_FORMAT_DOUBLE) {
            m_volume = *static_cast<double*>(prop->data);
            emit volumeChanged(m_volume);
        } else if (QLatin1String(prop->name) == "audio-bitrate" && prop->format == MPV_FORMAT_DOUBLE) {
            double bitsPerSecond = *static_cast<double*>(prop->data);
            emit bitrateChanged(bitsPerSecond / 1000.0);
        } else if (QLatin1String(prop->name) == "af-metadata/bouncy" && prop->format == MPV_FORMAT_NODE) {
            // The "bouncy" label is set on an astats filter in the af chain
            // (see EqualizerWidget::buildFilterChain); its metadata comes
            // back as a string-keyed map, one entry per ffmpeg astats key.
            auto* node = static_cast<mpv_node*>(prop->data);
            if (node->format == MPV_FORMAT_NODE_MAP && node->u.list) {
                for (int i = 0; i < node->u.list->num; ++i) {
                    if (QLatin1String(node->u.list->keys[i]) != "lavfi.astats.Overall.RMS_level")
                        continue;
                    mpv_node& value = node->u.list->values[i];
                    if (value.format != MPV_FORMAT_STRING)
                        break;
                    bool ok = false;
                    double rmsDb = QString::fromUtf8(value.u.string).toDouble(&ok);
                    if (ok) {
                        const double floorDb = -45.0;
                        double clamped = std::clamp(rmsDb, floorDb, 0.0);
                        emit bounceLevel((clamped - floorDb) / -floorDb);
                    }
                    break;
                }
            }
        }
        break;
    }
    case MPV_EVENT_START_FILE:
        emit playbackStarted();
        break;
    case MPV_EVENT_END_FILE:
        emit playbackStopped();
        break;
    default:
        break;
    }
}

void MpvPlayer::play(const QString& url)
{
    if (!m_mpv)
        return;
    QByteArray urlBytes = url.toUtf8();
    const char* cmd[] = { "loadfile", urlBytes.constData(), nullptr };
    mpv_command(m_mpv, cmd);
}

void MpvPlayer::stop()
{
    if (!m_mpv)
        return;
    mpv_command_string(m_mpv, "stop");
}

void MpvPlayer::togglePause()
{
    setPaused(!m_paused);
}

void MpvPlayer::setPaused(bool paused)
{
    if (!m_mpv)
        return;
    int flag = paused ? 1 : 0;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag);
}

void MpvPlayer::toggleMute()
{
    if (!m_mpv)
        return;
    mpv_command_string(m_mpv, "cycle mute");
}

void MpvPlayer::setVolume(double volume)
{
    if (!m_mpv)
        return;
    m_volume = volume;
    mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &volume);
}

void MpvPlayer::fadeVolumeTo(double target, int durationMs)
{
    if (!m_mpv)
        return;
    if (durationMs <= 0) {
        setVolume(target);
        return;
    }
    m_fadeStart = m_volume;
    m_fadeTarget = target;
    m_fadeDurationMs = durationMs;
    m_fadeClock.restart();
    m_fadeTimer->start();
}

void MpvPlayer::onFadeTick()
{
    double elapsed = m_fadeClock.elapsed();
    if (elapsed >= m_fadeDurationMs) {
        setVolume(m_fadeTarget);
        m_fadeTimer->stop();
        return;
    }
    double t = elapsed / static_cast<double>(m_fadeDurationMs);
    double vol = m_fadeStart + (m_fadeTarget - m_fadeStart) * t;
    setVolume(vol);
}

void MpvPlayer::setAudioFilterChain(const QString& af)
{
    if (!m_mpv)
        return;
    QByteArray bytes = af.toUtf8();
    mpv_set_property_string(m_mpv, "af", bytes.constData());
}
