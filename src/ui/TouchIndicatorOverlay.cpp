// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/TouchIndicatorOverlay.h"

#include <QGuiApplication>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QWindow>

namespace xen {

namespace {
// Faint, near-white grey ripple. Kept subtle: the strongest element is only
// ~20% opaque (80% transparent), per the requested look.
const QColor kInk(232, 232, 236);
constexpr int kMaxAlpha = 51; // 20% of 255
}

TouchIndicatorOverlay::TouchIndicatorOverlay(QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("Xeneon Edge — Touch Ripple"));
    // The app-wide stylesheet would otherwise paint an opaque background on a
    // styled widget, defeating WA_TranslucentBackground — force transparency.
    setStyleSheet(QStringLiteral("background: transparent;"));

    m_clock.start();
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this] {
        // Age released ripples and drop expired ones; repaint only if anything is live.
        bool live = false;
        QList<int> dead;
        for (auto it = m_ripples.begin(); it != m_ripples.end(); ++it) {
            if (it->active)
                live = true;
            else {
                it->ageMs += 16;
                if (it->ageMs > 450)
                    dead.append(it.key());
                else
                    live = true;
            }
        }
        for (int k : dead)
            m_ripples.remove(k);
        if (live || !dead.isEmpty())
            update();
    });
    m_timer->start(16);
}

void TouchIndicatorOverlay::showOnScreen(QScreen* screen)
{
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    winId();
    if (windowHandle())
        windowHandle()->setScreen(screen);
    setGeometry(screen->geometry());
    showFullScreen();
    raise();
}

void TouchIndicatorOverlay::onTouch(int id, TouchEventSource::Phase phase, double nx, double ny)
{
    const QPointF p(nx * width(), ny * height());
    Ripple& r = m_ripples[id];
    r.pos = p;
    r.ageMs = 0;
    r.active = phase != TouchEventSource::Phase::End;
    update();
}

void TouchIndicatorOverlay::paintEvent(QPaintEvent*)
{
    // WA_TranslucentBackground clears the backing store to fully transparent
    // each frame, so we only paint the ripples themselves.
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);

    const int frame = static_cast<int>(m_clock.elapsed() / 16);

    for (auto it = m_ripples.constBegin(); it != m_ripples.constEnd(); ++it) {
        const Ripple& r = it.value();
        const QPointF c = r.pos;
        const qreal fade = r.active ? 1.0 : qMax(0.0, 1.0 - r.ageMs / 450.0);

        if (r.active) {
            const qreal phase = (frame % 40) / 40.0;
            const qreal rr = 20 + phase * 40;
            g.setPen(QPen(QColor(kInk.red(), kInk.green(), kInk.blue(),
                                 int(kMaxAlpha * (1.0 - phase))), 3));
            g.setBrush(Qt::NoBrush);
            g.drawEllipse(c, rr, rr);
        }

        // Soft filled dot, near-white and faint.
        g.setPen(Qt::NoPen);
        g.setBrush(QColor(kInk.red(), kInk.green(), kInk.blue(), int(kMaxAlpha * fade)));
        g.drawEllipse(c, 16, 16);
        // Slightly brighter core so the exact point is still readable.
        g.setBrush(QColor(255, 255, 255, int((kMaxAlpha + 20) * fade)));
        g.drawEllipse(c, 6, 6);
    }
}

} // namespace xen
