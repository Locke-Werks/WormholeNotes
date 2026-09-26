#pragma once

#include <QColor>
#include <QFont>
#include <QPainterPath>
#include <QPointF>
#include <QString>

class QFontMetricsF;
class QPainter;

// Geometry and text on circles. Angles are radians in screen convention: 0 is
// three o'clock and angles grow clockwise, so -pi/2 is noon.
//
// Text follows the arc it sits on. In the upper half it reads clockwise with
// its feet toward the centre; in the lower half ("flipped") it reads
// counter-clockwise with its head toward the centre, so both come out upright.
namespace Arc {

constexpr qreal kPi = 3.14159265358979323846;
constexpr qreal kTau = 2 * kPi;
constexpr qreal kNoon = -kPi / 2;
constexpr qreal kSix = kPi / 2;

inline qreal degrees(qreal radians) { return radians * 180.0 / kPi; }
inline qreal radians(qreal degrees) { return degrees * kPi / 180.0; }

QPointF polar(const QPointF &center, qreal radius, qreal angle);
qreal angleOf(const QPointF &center, const QPointF &point);

// Into [0, 2pi).
qreal wrapPositive(qreal angle);
// Into (-pi, pi].
qreal wrap(qreal angle);
// True when `angle` lies within the arc that starts at `from` and runs
// clockwise for `sweep`.
bool within(qreal angle, qreal from, qreal sweep);

// Whether text centred at this angle should be drawn flipped to stay upright.
bool flipsAt(qreal angle);

// A ring segment between two radii, running clockwise from `from` for `sweep`.
QPainterPath sector(const QPointF &center, qreal inner, qreal outer, qreal from, qreal sweep);

// Width of the text laid along a line, and the angle that takes at `radius`.
qreal advance(const QFontMetricsF &metrics, const QString &text, qreal tracking = 0);
qreal sweepOf(const QFontMetricsF &metrics, const QString &text, qreal radius, qreal tracking = 0);
QString elide(const QFontMetricsF &metrics, const QString &text, qreal maxLength);

// Draws text whose reading direction begins at angle `from`.
void drawFrom(QPainter &painter, const QPointF &center, qreal radius, qreal from, const QString &text,
              const QFont &font, const QColor &color, bool flipped, qreal tracking = 0);

// Draws text centred on angle `mid`, flipped automatically in the lower half.
void drawCentered(QPainter &painter, const QPointF &center, qreal radius, qreal mid, const QString &text,
                  const QFont &font, const QColor &color, qreal tracking = 0);

// Removes the '&' mnemonic markers from an action's text, reporting the
// mnemonic character if there is one.
QString stripMnemonic(const QString &text, QChar *mnemonic = nullptr);

} // namespace Arc
