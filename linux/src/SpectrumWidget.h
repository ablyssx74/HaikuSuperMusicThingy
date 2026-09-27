/*
 * Copyright 2026, ablyss supermusicthingy@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

#include <QWidget>
#include <QColor>
#include <QElapsedTimer>
#include <array>

class QTimer;
class QImage;

// A 64-bar audio-reactive spectrum, ported from the Haiku build's
// SpectrumView MODE_BARS (haiku-supermusicthingy.cpp). The Haiku app isn't
// a true per-frequency FFT analyzer either: a single overall audio level
// (from mpv's astats filter) drives 64 bars through per-bar frequency-scale
// curves, organic sine/cosine jitter and spring physics, so that port
// carries over directly. Bar colors come from a 64-entry palette sampled
// from the station's album art (AdaptToAlbumArt in the Haiku source),
// falling back to the same cyan-to-blue gradient the Haiku build defaults
// to when there's no artwork yet.
class SpectrumWidget : public QWidget {
    Q_OBJECT
public:
    explicit SpectrumWidget(QWidget* parent = nullptr);

    QSize sizeHint() const override { return QSize(400, 72); }

public slots:
    // level is normalized 0..1 overall audio magnitude (0 = silence).
    void setLevel(double level);
    void setPaletteFromImage(const QImage& image);
    void resetToDefaultPalette();

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onTick();

private:
    static constexpr int kBarCount = 64;

    std::array<QColor, kBarCount> m_palette;
    std::array<float, kBarCount> m_barHeights{};
    std::array<float, kBarCount> m_barVelocities{};
    std::array<float, kBarCount> m_peakHeights{};
    std::array<float, kBarCount> m_peakHoldFrames{};

    double m_targetLevel = 0.0;
    double m_smoothedLevel = 0.0;

    QTimer* m_timer;
    QElapsedTimer m_clock;
    qint64 m_lastTickMs = 0;
};
