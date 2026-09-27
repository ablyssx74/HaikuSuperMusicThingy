/*
 * Copyright 2026, Kris Beazley supermusicthingy@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

#include <QWidget>
#include <array>

class QSlider;
class QCheckBox;
class QLabel;
struct AppConfig;

// 15-band equalizer + limiter, built from ffmpeg's `equalizer`/`alimiter`
// audio filters (mpv's built-in libavfilter support) instead of the Haiku
// build's optional LADSPA (mbeq_1197 / fast_lookahead_limiter_1913) path,
// which isn't installed at the same paths on Linux.
class EqualizerWidget : public QWidget {
    Q_OBJECT
public:
    explicit EqualizerWidget(QWidget* parent = nullptr);

    void loadFromConfig(const AppConfig& config);
    void saveToConfig(AppConfig& config) const;

    // Builds the mpv `af` filter chain string for the current slider state,
    // or an empty string when the EQ is disabled.
    QString buildFilterChain() const;

    bool isEnabled() const;

signals:
    void filterChainChanged(const QString& af);

private slots:
    void applyPreset(const float* gains);
    void emitFilterChain();

private:
    QSlider* m_bandSliders[15];
    QLabel* m_bandValueLabels[15];
    QCheckBox* m_enableCheck;
    QSlider* m_limitInputSlider;
    QSlider* m_limitThresholdSlider;
    QSlider* m_limitReleaseSlider;
};
