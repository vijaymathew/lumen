#include "ui/AppIcon.h"

#include <QColor>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRadialGradient>

#include <algorithm>

namespace {

// Draws the mark filling an s×s square (transparent background elsewhere).
void paintMark(QPainter &p, int s)
{
    const qreal S = s;
    p.setRenderHint(QPainter::Antialiasing, true);

    // Rounded tile with a diagonal violet→indigo gradient (the app accent).
    QLinearGradient bg(0, 0, S, S);
    bg.setColorAt(0.0, QColor(0x9a, 0x8c, 0xf0));
    bg.setColorAt(1.0, QColor(0x4b, 0x3f, 0xa8));
    QPainterPath tile;
    tile.addRoundedRect(QRectF(0.5, 0.5, S - 1.0, S - 1.0), S * 0.22, S * 0.22);
    p.fillPath(tile, bg);

    const QPointF c(S / 2.0, S / 2.0);

    // A soft glowing orb — the "lumen" (light).
    QRadialGradient glow(c, S * 0.30);
    glow.setColorAt(0.0, QColor(255, 255, 255, 255));
    glow.setColorAt(0.55, QColor(255, 255, 255, 150));
    glow.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(c, S * 0.30, S * 0.30);

    // A thin ring framing the orb (reads as a lens).
    QPen ring(QColor(255, 255, 255, 235));
    ring.setWidthF(std::max(1.0, S * 0.055));
    p.setPen(ring);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, S * 0.33, S * 0.33);
}

} // namespace

QIcon makeAppIcon()
{
    QIcon icon;
    // Render each standard size directly (crisper than downscaling one big pixmap).
    for (int s : {16, 24, 32, 48, 64, 128, 256}) {
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paintMark(p, s);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}
