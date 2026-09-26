#include "Arc.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QTextBoundaryFinder>

#include <cmath>

namespace Arc {

namespace {

// Grapheme clusters with their advances, so a combining mark or a surrogate
// pair is placed as one piece.
QList<QPair<QString, qreal>> clusters(const QFontMetricsF &metrics, const QString &text, qreal tracking)
{
    QList<QPair<QString, qreal>> out;
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    qsizetype previous = 0;
    for (qsizetype next = finder.toNextBoundary(); next != -1; next = finder.toNextBoundary()) {
        const QString cluster = text.mid(previous, next - previous);
        out.append({ cluster, metrics.horizontalAdvance(cluster) + tracking });
        previous = next;
    }
    return out;
}

} // namespace

QPointF polar(const QPointF &center, qreal radius, qreal angle)
{
    return center + QPointF(radius * std::cos(angle), radius * std::sin(angle));
}

qreal angleOf(const QPointF &center, const QPointF &point)
{
    const QPointF v = point - center;
    return std::atan2(v.y(), v.x());
}

qreal wrapPositive(qreal angle)
{
    angle = std::fmod(angle, kTau);
    return angle < 0 ? angle + kTau : angle;
}

qreal wrap(qreal angle)
{
    angle = wrapPositive(angle);
    return angle > kPi ? angle - kTau : angle;
}

bool within(qreal angle, qreal from, qreal sweep)
{
    return wrapPositive(angle - from) <= sweep;
}

bool flipsAt(qreal angle)
{
    return std::sin(angle) > 0.08;
}

QPainterPath sector(const QPointF &center, qreal inner, qreal outer, qreal from, qreal sweep)
{
    // QPainterPath::arcTo takes degrees counter-clockwise; ours are radians
    // clockwise, hence the sign flips.
    QPainterPath path;
    const QRectF outerRect(center.x() - outer, center.y() - outer, 2 * outer, 2 * outer);
    const QRectF innerRect(center.x() - inner, center.y() - inner, 2 * inner, 2 * inner);
    path.moveTo(polar(center, outer, from));
    path.arcTo(outerRect, -degrees(from), -degrees(sweep));
    path.lineTo(polar(center, inner, from + sweep));
    path.arcTo(innerRect, -degrees(from + sweep), degrees(sweep));
    path.closeSubpath();
    return path;
}

qreal advance(const QFontMetricsF &metrics, const QString &text, qreal tracking)
{
    qreal total = 0;
    for (const auto &[cluster, width] : clusters(metrics, text, tracking))
        total += width;
    return total;
}

qreal sweepOf(const QFontMetricsF &metrics, const QString &text, qreal radius, qreal tracking)
{
    return radius > 0 ? advance(metrics, text, tracking) / radius : 0;
}

QString elide(const QFontMetricsF &metrics, const QString &text, qreal maxLength)
{
    return metrics.elidedText(text, Qt::ElideMiddle, qMax<qreal>(0, maxLength));
}

void drawFrom(QPainter &painter, const QPointF &center, qreal radius, qreal from, const QString &text,
              const QFont &font, const QColor &color, bool flipped, qreal tracking)
{
    if (radius <= 0 || text.isEmpty())
        return;
    const QFontMetricsF metrics(font);
    const qreal baseline = metrics.capHeight() / 2;
    const qreal direction = flipped ? -1 : 1;

    painter.save();
    painter.setFont(font);
    painter.setPen(color);
    qreal angle = from;
    for (const auto &[cluster, width] : clusters(metrics, text, tracking)) {
        const qreal mid = angle + direction * (width / 2) / radius;
        painter.save();
        painter.translate(polar(center, radius, mid));
        painter.rotate(degrees(mid) + (flipped ? -90 : 90));
        painter.drawText(QPointF(-(width - tracking) / 2, baseline), cluster);
        painter.restore();
        angle += direction * width / radius;
    }
    painter.restore();
}

void drawCentered(QPainter &painter, const QPointF &center, qreal radius, qreal mid, const QString &text,
                  const QFont &font, const QColor &color, qreal tracking)
{
    const bool flipped = flipsAt(mid);
    const qreal sweep = sweepOf(QFontMetricsF(font), text, radius, tracking);
    const qreal from = flipped ? mid + sweep / 2 : mid - sweep / 2;
    drawFrom(painter, center, radius, from, text, font, color, flipped, tracking);
}

QString stripMnemonic(const QString &text, QChar *mnemonic)
{
    QString out;
    out.reserve(text.size());
    if (mnemonic)
        *mnemonic = QChar();
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == u'&' && i + 1 < text.size()) {
            const QChar next = text.at(i + 1);
            if (next != u'&' && mnemonic && mnemonic->isNull())
                *mnemonic = next.toLower();
            out += next;
            ++i;
            continue;
        }
        out += ch;
    }
    return out;
}

} // namespace Arc
