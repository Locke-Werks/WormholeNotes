#include "Round.h"

#include <QFontMetricsF>
#include <QPainter>

#include <cmath>
#include <numbers>

namespace Round {

qreal radians(qreal degrees)
{
    return degrees * std::numbers::pi / 180.0;
}

qreal degrees(qreal radians)
{
    return radians * 180.0 / std::numbers::pi;
}

QPointF polar(const QPointF &center, qreal radius, qreal angle)
{
    return center + QPointF(std::cos(angle), std::sin(angle)) * radius;
}

qreal angleOf(const QPointF &center, const QPointF &point)
{
    const QPointF v = point - center;
    return std::atan2(v.y(), v.x());
}

qreal between(qreal a, qreal b)
{
    const qreal turn = 2 * std::numbers::pi;
    qreal d = std::fmod(std::abs(a - b), turn);
    return d > turn / 2 ? turn - d : d;
}

qreal arcSpan(const QString &text, const QFont &font, qreal radius)
{
    return radius > 0 ? QFontMetricsF(font).horizontalAdvance(text) / radius : 0;
}

QString fitArc(const QString &text, const QFont &font, qreal radius, qreal span)
{
    const QFontMetricsF metrics(font);
    return metrics.elidedText(text, Qt::ElideRight, span * radius);
}

void arcText(QPainter &p, const QPointF &center, qreal radius, qreal angle, const QString &text, const QFont &font,
             const QColor &color)
{
    if (text.isEmpty())
        return;
    const QFontMetricsF metrics(font);
    // In the lower half the letters run anticlockwise so they stay upright.
    const bool below = std::sin(angle) > 0.2;
    const qreal direction = below ? -1.0 : 1.0;
    const qreal span = metrics.horizontalAdvance(text) / radius;
    qreal at = angle - direction * span / 2;
    // Centre the glyphs vertically on the circle.
    const qreal baseline = (metrics.ascent() - metrics.descent()) / 2;

    p.save();
    p.setFont(font);
    p.setPen(color);
    for (const QChar ch : text) {
        const qreal advance = metrics.horizontalAdvance(ch);
        const qreal mid = at + direction * advance / 2 / radius;
        p.save();
        p.translate(polar(center, radius, mid));
        p.rotate(degrees(mid) + (below ? -90 : 90));
        p.drawText(QPointF(-advance / 2, baseline), QString(ch));
        p.restore();
        at += direction * advance / radius;
    }
    p.restore();
}

} // namespace Round
