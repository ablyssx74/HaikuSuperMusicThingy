/*
 * Copyright 2026, ablyss supermusicthingy@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "SpectrumWidget.h"

#include <QTimer>
#include <QImage>
#include <QPainter>
#include <cmath>
#include <algorithm>

SpectrumWidget::SpectrumWidget(QWidget* parent)
    : QWidget(parent)
{
    resetToDefaultPalette();

    m_timer = new QTimer(this);
    m_timer->setInterval(33); // ~30 FPS, matching the Haiku build's tick rate
    connect(m_timer, &QTimer::timeout, this, &SpectrumWidget::onTick);
    m_clock.start();
    m_timer->start();
}

void SpectrumWidget::resetToDefaultPalette()
{
    for (int i = 0; i < kBarCount; ++i) {
        m_palette[i] = QColor(40 + i * 2, 210, 255 - i * 3);
    }
    update();
}

void SpectrumWidget::setPaletteFromImage(const QImage& imageIn)
{
    if (imageIn.isNull() || imageIn.width() <= 0 || imageIn.height() <= 0) {
        resetToDefaultPalette();
        return;
    }

    QImage image = imageIn.convertToFormat(QImage::Format_RGB32);
    const int width = image.width();
    const int height = image.height();
    const int rows[3] = {
        std::clamp(static_cast<int>(height * 0.30), 0, height - 1),
        std::clamp(static_cast<int>(height * 0.55), 0, height - 1),
        std::clamp(static_cast<int>(height * 0.75), 0, height - 1),
    };

    for (int i = 0; i < kBarCount; ++i) {
        double horizontalPercent = static_cast<double>(i) / kBarCount;
        int x = std::clamp(static_cast<int>(horizontalPercent * width), 0, width - 1);

        int sumR = 0, sumG = 0, sumB = 0;
        for (int r : rows) {
            QRgb px = image.pixel(x, r);
            sumR += qRed(px);
            sumG += qGreen(px);
            sumB += qBlue(px);
        }
        int finalR = sumR / 3;
        int finalG = sumG / 3;
        int finalB = sumB / 3;

        // Boost artwork that's too dark to register as visible bars.
        if (finalR < 35 && finalG < 35 && finalB < 35) {
            int maxChannel = std::max({finalR, finalG, finalB});
            if (maxChannel == 0) {
                m_palette[i] = QColor(40, 50, 60);
            } else {
                double boost = 60.0 / maxChannel;
                m_palette[i] = QColor(std::min(255, static_cast<int>(finalR * boost)),
                                       std::min(255, static_cast<int>(finalG * boost)),
                                       std::min(255, static_cast<int>(finalB * boost)));
            }
        } else {
            m_palette[i] = QColor(finalR, finalG, finalB);
        }
    }
    update();
}

void SpectrumWidget::setLevel(double level)
{
    m_targetLevel = std::clamp(level, 0.0, 1.0);
}

void SpectrumWidget::onTick()
{
    qint64 nowMs = m_clock.elapsed();
    double dtSeconds = (nowMs - m_lastTickMs) / 1000.0;
    m_lastTickMs = nowMs;
    if (dtSeconds <= 0.0 || dtSeconds > 0.25)
        dtSeconds = 1.0 / 30.0;

    // Matches the Haiku build's 20 FPS (0.05s) baseline for its tuning constants.
    double dtScale = std::min(dtSeconds / 0.05, 3.0);

    // Instant attack, exponential decay - same envelope shape as fCurrentLevel.
    if (m_targetLevel > m_smoothedLevel)
        m_smoothedLevel = m_targetLevel;
    else
        m_smoothedLevel = m_smoothedLevel * std::pow(0.80, dtScale) + m_targetLevel * (1.0 - std::pow(0.80, dtScale));

    const double viewHeight = height();
    const double springStiffness = 0.95 * dtScale;
    const double springDamping = std::pow(0.55, dtScale);
    const double dynamicTimePhase = nowMs / 35.0;
    const double visualizerHeightBoost = 1.5;

    // Squaring compresses the response curve so ordinary program material
    // (which sits well above silence but rarely near 0 dBFS) stays visually
    // calm instead of pinning every bar to the top; only real peaks read
    // tall. Matches the Haiku build's masterMagnitude calculation exactly.
    const double masterSensitivityMultiplier = 0.87;
    const double magnitude = m_smoothedLevel * m_smoothedLevel * masterSensitivityMultiplier;

    for (int i = 0; i < kBarCount; ++i) {
        double frequencyScale = 1.0;
        if (i < 12)
            frequencyScale = 1.15 + (12 - i) * 0.03;
        else if (i > 45)
            frequencyScale = 0.85 - (i - 45) * 0.02;

        double fastHarmonicWave = std::sin(dynamicTimePhase + i * 0.45) * 0.08;
        double chaoticNoise = std::cos(dynamicTimePhase * 1.6 - i * 0.75) * 0.06;
        double audioJitterMultiplier = 1.0 + (fastHarmonicWave + chaoticNoise) * (magnitude * 1.5);
        double organicScale = (0.95 + 0.10 * std::sin(i * 0.25)) * audioJitterMultiplier;

        double targetHeight = magnitude * viewHeight * frequencyScale * organicScale * visualizerHeightBoost;
        targetHeight = std::min(targetHeight, viewHeight);

        double displacement = targetHeight - m_barHeights[i];
        double springForce = displacement * springStiffness;
        m_barVelocities[i] = static_cast<float>((m_barVelocities[i] + springForce) * springDamping);
        m_barHeights[i] += static_cast<float>(m_barVelocities[i] * dtScale);
        m_barHeights[i] = std::clamp(m_barHeights[i], 0.0f, static_cast<float>(viewHeight));

        if (m_barHeights[i] >= m_peakHeights[i]) {
            m_peakHeights[i] = m_barHeights[i];
            m_peakHoldFrames[i] = static_cast<float>(6.0 * dtScale);
        } else if (m_peakHoldFrames[i] > 0.0f) {
            m_peakHoldFrames[i] -= static_cast<float>(dtScale);
        } else {
            m_peakHeights[i] -= static_cast<float>(viewHeight * 0.035 * dtScale);
            m_peakHeights[i] = std::max(m_peakHeights[i], 0.0f);
        }
    }

    update();
}

void SpectrumWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect(), QColor(0, 0, 0, 40));

    const double w = width();
    const double h = height();
    const double barWidth = w / kBarCount;

    for (int i = 0; i < kBarCount; ++i) {
        double x0 = i * barWidth;
        double x1 = (i + 1) * barWidth - 1.0;
        if (x1 < x0)
            x1 = x0;

        QColor barColor = m_palette[i];
        QRectF barRect(x0, h - m_barHeights[i], x1 - x0, m_barHeights[i]);
        painter.fillRect(barRect, barColor);

        if (m_peakHeights[i] > m_barHeights[i] + 1.0f) {
            QColor peakColor = barColor.lighter(150);
            QRectF peakRect(x0, h - m_peakHeights[i] - 2.0, x1 - x0, 2.0);
            painter.fillRect(peakRect, peakColor);
        }
    }
}
