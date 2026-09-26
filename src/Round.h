#pragma once

#include <QColor>
#include <QFont>
#include <QPointF>
#include <QString>

class QPainter;

// Small helpers for things drawn in the round. Angles are in radians, screen
// convention: 0 at three o'clock, increasing clockwise, so noon is -pi/2.
namespace Round {

constexpr qreal kNoon = -1.5707963267948966;

qreal radians(qreal degrees);
qreal degrees(qreal radians);
QPointF polar(const QPointF &center, qreal radius, qreal angle);
qreal angleOf(const QPointF &center, const QPointF &point);
// The shortest way round from a to b, as a positive angle.
qreal between(qreal a, qreal b);

// Draws text along a circle, centred on an angle, each character standing
// upright on the curve. Above the centre it reads clockwise; below, where
// clockwise text would hang upside down, it reads the other way.
void arcText(QPainter &p, const QPointF &center, qreal radius, qreal angle, const QString &text, const QFont &font,
             const QColor &color);
// The angle a string takes up at that radius.
qreal arcSpan(const QString &text, const QFont &font, qreal radius);
// Cuts text to fit an angle, ending in an ellipsis.
QString fitArc(const QString &text, const QFont &font, qreal radius, qreal span);

} // namespace Round
