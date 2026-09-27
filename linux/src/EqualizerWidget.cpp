/*
 * Copyright 2026, ablyss supermusicthingy@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "EqualizerWidget.h"
#include "Channel.h"
#include "ConfigManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSlider>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <algorithm>
#include <cmath>

namespace {
// Sliders use tenths of a dB so QSlider (integer only) still gives smooth
// 0.1 dB steps, matching the resolution of the Haiku build's BSlider.
constexpr int kBandRange = 200; // +/- 20.0 dB
double sliderToDb(int value) { return value / 10.0; }
int dbToSlider(double db) { return static_cast<int>(std::lround(db * 10.0)); }
}

EqualizerWidget::EqualizerWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* rootLayout = new QVBoxLayout(this);

    m_enableCheck = new QCheckBox(tr("Enable Equalizer"), this);
    m_enableCheck->setChecked(true);
    rootLayout->addWidget(m_enableCheck);

    auto* bandsGroup = new QGroupBox(tr("15-Band Equalizer"), this);
    auto* bandsLayout = new QHBoxLayout(bandsGroup);
    for (int i = 0; i < 15; ++i) {
        auto* column = new QVBoxLayout();
        m_bandValueLabels[i] = new QLabel("0.0", bandsGroup);
        m_bandValueLabels[i]->setAlignment(Qt::AlignCenter);

        m_bandSliders[i] = new QSlider(Qt::Vertical, bandsGroup);
        m_bandSliders[i]->setRange(-kBandRange, kBandRange);
        m_bandSliders[i]->setValue(dbToSlider(kPresetRock[i]));
        m_bandSliders[i]->setMinimumHeight(120);

        QString freqLabel = kEqFrequencies[i] >= 1000
            ? QString("%1k").arg(kEqFrequencies[i] / 1000.0, 0, 'g', 3)
            : QString::number(static_cast<int>(kEqFrequencies[i]));
        auto* freqText = new QLabel(freqLabel, bandsGroup);
        freqText->setAlignment(Qt::AlignCenter);

        column->addWidget(m_bandValueLabels[i]);
        column->addWidget(m_bandSliders[i], 0, Qt::AlignHCenter);
        column->addWidget(freqText);
        bandsLayout->addLayout(column);

        connect(m_bandSliders[i], &QSlider::valueChanged, this, [this, i](int value) {
            m_bandValueLabels[i]->setText(QString::number(sliderToDb(value), 'f', 1));
            emitFilterChain();
        });
    }
    rootLayout->addWidget(bandsGroup);

    auto* presetsLayout = new QHBoxLayout();
    struct PresetButton { const char* name; const float* gains; };
    const PresetButton presets[] = {
        { "Flat", kPresetFlat },
        { "Rock", kPresetRock },
        { "Jazz", kPresetJazz },
        { "Bass Boost", kPresetBass },
    };
    for (const auto& preset : presets) {
        auto* button = new QPushButton(tr(preset.name), this);
        connect(button, &QPushButton::clicked, this, [this, preset]() {
            applyPreset(preset.gains);
        });
        presetsLayout->addWidget(button);
    }
    rootLayout->addLayout(presetsLayout);

    auto* limiterGroup = new QGroupBox(tr("Limiter"), this);
    auto* limiterLayout = new QGridLayout(limiterGroup);

    m_limitInputSlider = new QSlider(Qt::Horizontal, limiterGroup);
    m_limitInputSlider->setRange(-200, 200);
    m_limitInputSlider->setValue(0);
    limiterLayout->addWidget(new QLabel(tr("Input Gain (dB)"), limiterGroup), 0, 0);
    limiterLayout->addWidget(m_limitInputSlider, 0, 1);

    m_limitThresholdSlider = new QSlider(Qt::Horizontal, limiterGroup);
    m_limitThresholdSlider->setRange(-200, 0);
    m_limitThresholdSlider->setValue(0);
    limiterLayout->addWidget(new QLabel(tr("Limit Threshold (dB)"), limiterGroup), 1, 0);
    limiterLayout->addWidget(m_limitThresholdSlider, 1, 1);

    m_limitReleaseSlider = new QSlider(Qt::Horizontal, limiterGroup);
    m_limitReleaseSlider->setRange(10, 1000);
    m_limitReleaseSlider->setValue(100);
    limiterLayout->addWidget(new QLabel(tr("Release (ms)"), limiterGroup), 2, 0);
    limiterLayout->addWidget(m_limitReleaseSlider, 2, 1);

    rootLayout->addWidget(limiterGroup);
    rootLayout->addStretch();

    connect(m_enableCheck, &QCheckBox::toggled, this, &EqualizerWidget::emitFilterChain);
    connect(m_limitInputSlider, &QSlider::valueChanged, this, &EqualizerWidget::emitFilterChain);
    connect(m_limitThresholdSlider, &QSlider::valueChanged, this, &EqualizerWidget::emitFilterChain);
    connect(m_limitReleaseSlider, &QSlider::valueChanged, this, &EqualizerWidget::emitFilterChain);
}

void EqualizerWidget::applyPreset(const float* gains)
{
    for (int i = 0; i < 15; ++i)
        m_bandSliders[i]->setValue(dbToSlider(gains[i]));
    emitFilterChain();
}

bool EqualizerWidget::isEnabled() const
{
    return m_enableCheck->isChecked();
}

void EqualizerWidget::loadFromConfig(const AppConfig& config)
{
    m_enableCheck->setChecked(config.eqEnabled);
    for (int i = 0; i < 15; ++i)
        m_bandSliders[i]->setValue(dbToSlider(config.eqBands[i]));
    m_limitInputSlider->setValue(dbToSlider(config.limitInputGainDb));
    m_limitThresholdSlider->setValue(dbToSlider(config.limitThresholdDb));
    m_limitReleaseSlider->setValue(static_cast<int>(config.limitReleaseMs));
}

void EqualizerWidget::saveToConfig(AppConfig& config) const
{
    config.eqEnabled = m_enableCheck->isChecked();
    for (int i = 0; i < 15; ++i)
        config.eqBands[i] = static_cast<float>(sliderToDb(m_bandSliders[i]->value()));
    config.limitInputGainDb = static_cast<float>(sliderToDb(m_limitInputSlider->value()));
    config.limitThresholdDb = static_cast<float>(sliderToDb(m_limitThresholdSlider->value()));
    config.limitReleaseMs = static_cast<float>(m_limitReleaseSlider->value());
}

QString EqualizerWidget::buildFilterChain() const
{
    // A labeled astats tap ("bouncy") feeds the spectrum visualizer's level
    // meter via mpv's af-metadata property (see MpvPlayer::handleEvent). It
    // stays in the chain even with the EQ off so the spectrum keeps working.
    static const QString kLevelMeterTap = QStringLiteral("asetnsamples=n=1024,@bouncy:astats=metadata=1:reset=1");

    if (!m_enableCheck->isChecked())
        return kLevelMeterTap;

    QString chain;
    for (int i = 0; i < 15; ++i) {
        double gain = sliderToDb(m_bandSliders[i]->value());
        chain += QString("equalizer=f=%1:width_type=o:w=1:g=%2,")
                     .arg(kEqFrequencies[i], 0, 'f', 0)
                     .arg(gain, 0, 'f', 2);
    }

    // Same dB-to-linear conversion the Haiku build uses for mpv's alimiter filter.
    double inputGain = std::pow(10.0, sliderToDb(m_limitInputSlider->value()) / 20.0);
    double limit = std::pow(10.0, sliderToDb(m_limitThresholdSlider->value()) / 20.0);
    inputGain = std::max(inputGain, 0.001);
    limit = std::max(limit, 0.001);

    chain += QString("alimiter=level_in=%1:limit=%2:release=%3,")
                 .arg(inputGain, 0, 'f', 2)
                 .arg(limit, 0, 'f', 2)
                 .arg(m_limitReleaseSlider->value());
    chain += kLevelMeterTap;

    return chain;
}

void EqualizerWidget::emitFilterChain()
{
    emit filterChainChanged(buildFilterChain());
}
