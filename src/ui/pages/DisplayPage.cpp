// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/pages/DisplayPage.h"

#include "core/AppSettings.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace xen {

namespace {

struct PresetEntry {
    const char* label;
    quint16 value;
};
// Values from the Edge's own DDC capabilities string (VCP 0x14).
constexpr PresetEntry kPresets[] = {
    { "sRGB",           0x01 },
    { "Display Native", 0x02 },
    { "5000 K",         0x04 },
    { "6500 K",         0x05 },
    { "7500 K",         0x06 },
    { "9300 K",         0x08 },
    { "User 1",         0x0B },
};

} // namespace

DisplayPage::DisplayPage(DdcClient* ddc, QWidget* parent)
    : QWidget(parent)
    , m_ddc(ddc)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(24, 24, 24, 24);
    lay->setSpacing(16);

    m_status = new QLabel(tr("Looking for the Edge over DDC/CI…"), this);
    m_status->setObjectName(QStringLiteral("cardSubtitle"));
    lay->addWidget(m_status);

    lay->addWidget(buildPictureCard());
    lay->addWidget(buildColorCard());
    lay->addWidget(buildPowerCard());
    lay->addStretch(1);

    connect(m_ddc, &DdcClient::readyChanged, this, &DisplayPage::onReady);
    connect(m_ddc, &DdcClient::vcpRead, this, &DisplayPage::onVcpRead);
    connect(m_ddc, &DdcClient::errorOccurred, this, &DisplayPage::onError);

    setEnabled(false); // until DDC bus detection completes
}

QWidget* DisplayPage::buildPictureCard()
{
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("card"));
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(20, 16, 20, 16);

    auto* title = new QLabel(tr("PICTURE"), card);
    title->setObjectName(QStringLiteral("cardTitle"));
    v->addWidget(title);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(10);
    grid->setColumnStretch(1, 1);
    addSliderRow(grid, 0, tr("Brightness"), vcp::kBrightness);
    addSliderRow(grid, 1, tr("Contrast"), vcp::kContrast);
    addSliderRow(grid, 2, tr("Sharpness"), vcp::kSharpness);
    v->addLayout(grid);
    return card;
}

QWidget* DisplayPage::buildColorCard()
{
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("card"));
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(20, 16, 20, 16);

    auto* title = new QLabel(tr("COLOR"), card);
    title->setObjectName(QStringLiteral("cardTitle"));
    v->addWidget(title);

    auto* presetRow = new QHBoxLayout;
    auto* presetLabel = new QLabel(tr("Preset"), card);
    presetLabel->setObjectName(QStringLiteral("formKey"));
    presetLabel->setFixedWidth(110);
    m_preset = new QComboBox(card);
    for (const auto& p : kPresets)
        m_preset->addItem(QString::fromLatin1(p.label), p.value);
    connect(m_preset, qOverload<int>(&QComboBox::activated), this, [this](int idx) {
        const quint16 val = static_cast<quint16>(m_preset->itemData(idx).toUInt());
        m_ddc->setVcp(vcp::kPreset, val);
        settings::saveVcp(vcp::kPreset, val);
        // Preset changes move the gains; re-read them.
        m_ddc->getVcp(vcp::kGainRed);
        m_ddc->getVcp(vcp::kGainGreen);
        m_ddc->getVcp(vcp::kGainBlue);
    });
    presetRow->addWidget(presetLabel);
    presetRow->addWidget(m_preset, 1);
    v->addLayout(presetRow);
    v->addSpacing(6);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(10);
    grid->setColumnStretch(1, 1);
    addSliderRow(grid, 0, tr("Red gain"), vcp::kGainRed);
    addSliderRow(grid, 1, tr("Green gain"), vcp::kGainGreen);
    addSliderRow(grid, 2, tr("Blue gain"), vcp::kGainBlue);
    v->addLayout(grid);
    return card;
}

QWidget* DisplayPage::buildPowerCard()
{
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("card"));
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(20, 16, 20, 16);

    auto* title = new QLabel(tr("POWER"), card);
    title->setObjectName(QStringLiteral("cardTitle"));
    v->addWidget(title);

    auto* row = new QHBoxLayout;
    auto* off = new QPushButton(tr("Screen off"), card);
    auto* on = new QPushButton(tr("Screen on"), card);
    off->setObjectName(QStringLiteral("actionButton"));
    on->setObjectName(QStringLiteral("actionButton"));
    connect(off, &QPushButton::clicked, this, [this] { m_ddc->setVcp(vcp::kPower, 0x05); });
    connect(on, &QPushButton::clicked, this, [this] { m_ddc->setVcp(vcp::kPower, 0x01); });
    row->addWidget(off);
    row->addWidget(on);
    row->addStretch(1);
    v->addLayout(row);

    auto* note = new QLabel(tr("Turns only the Edge panel off; the app and touch stay live."), card);
    note->setObjectName(QStringLiteral("cardSubtitle"));
    v->addWidget(note);
    return card;
}

void DisplayPage::addSliderRow(QGridLayout* grid, int row, const QString& label, quint8 code)
{
    auto* key = new QLabel(label, this);
    key->setObjectName(QStringLiteral("formKey"));
    key->setFixedWidth(110);

    auto* slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(0, 100);

    auto* value = new QLabel(QStringLiteral("—"), this);
    value->setObjectName(QStringLiteral("formValue"));
    value->setFixedWidth(40);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(250);

    connect(slider, &QSlider::valueChanged, this, [value, debounce](int v) {
        value->setText(QString::number(v));
        debounce->start();
    });
    connect(debounce, &QTimer::timeout, this, [this, slider, code] {
        const quint16 v = static_cast<quint16>(slider->value());
        m_ddc->setVcp(code, v);
        settings::saveVcp(code, v);
    });

    grid->addWidget(key, row, 0);
    grid->addWidget(slider, row, 1);
    grid->addWidget(value, row, 2);
    m_rows.insert(code, { slider, value, debounce });
}

void DisplayPage::showEvent(QShowEvent* ev)
{
    QWidget::showEvent(ev);
    if (m_ddc->ready() && !m_refreshed)
        refresh();
}

void DisplayPage::refresh()
{
    m_refreshed = true;
    for (auto it = m_rows.constBegin(); it != m_rows.constEnd(); ++it)
        m_ddc->getVcp(it.key());
    m_ddc->getVcp(vcp::kPreset);
}

void DisplayPage::onReady(bool ready, const QString& message)
{
    m_status->setText(message);
    setEnabled(ready);
    if (ready && isVisible() && !m_refreshed)
        refresh();
}

void DisplayPage::onVcpRead(quint8 code, quint16 current, quint16 max)
{
    if (code == vcp::kPreset) {
        const int idx = m_preset->findData(current);
        if (idx >= 0) {
            QSignalBlocker b(m_preset);
            m_preset->setCurrentIndex(idx);
        }
        return;
    }
    auto it = m_rows.find(code);
    if (it == m_rows.end())
        return;
    if (max > 0)
        it->slider->setMaximum(max);
    {
        QSignalBlocker b(it->slider);
        it->slider->setValue(current);
    }
    it->value->setText(QString::number(current));
}

void DisplayPage::onError(const QString& message)
{
    m_status->setText(message);
}

} // namespace xen
