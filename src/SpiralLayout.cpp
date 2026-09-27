#include "SpiralLayout.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPolygonF>
#include <QTextBlock>
#include <QTextDocument>

#include <cmath>
#include <numbers>

namespace {

constexpr qreal kTurn = 2 * std::numbers::pi;
// Inset of the first turn from the rim, as a share of the radius.
constexpr qreal kInset = 0.05;
// The spiral stops this far out, as a share of the radius, leaving the
// centre for the page controls.
constexpr qreal kCentre = 0.30;

bool isBreak(QChar ch)
{
    return ch.isSpace() || ch == QChar::ParagraphSeparator;
}

} // namespace

void SpiralLayout::setFont(const QFont &font)
{
    m_font = font;
}

void SpiralLayout::setDiameter(qreal diameter)
{
    m_diameter = diameter;
}

qreal SpiralLayout::radiusAt(qreal theta) const
{
    return m_outerRadius - m_pitch * theta / kTurn;
}

QPointF SpiralLayout::pointAt(qreal theta, qreal offset) const
{
    const qreal r = m_diameter / 2;
    const qreal screen = -std::numbers::pi / 2 + theta;
    return QPointF(r, r) + QPointF(std::cos(screen), std::sin(screen)) * (radiusAt(theta) + offset);
}

qreal SpiralLayout::maxTheta() const
{
    return m_pitch > 0 ? (m_outerRadius - m_innerRadius) / m_pitch * kTurn : 0;
}

void SpiralLayout::layout(const QTextDocument *document)
{
    m_stops.clear();
    m_glyphs.clear();
    m_pages = 1;

    const QFontMetricsF metrics(m_font);
    m_pitch = metrics.lineSpacing() * 1.08;
    m_baseline = (metrics.ascent() - metrics.descent()) / 2;
    const qreal r = m_diameter / 2;
    m_outerRadius = r * (1 - kInset) - m_pitch / 2;
    m_innerRadius = qMax(r * kCentre, m_pitch * 2.2);
    if (m_outerRadius <= m_innerRadius + m_pitch)
        m_innerRadius = qMax(1.0, m_outerRadius - m_pitch);

    // The document as one run of characters, a paragraph separator between
    // blocks, so that index i is cursor position i.
    QString text;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        if (block != document->begin())
            text.append(QChar::ParagraphSeparator);
        text.append(block.text());
    }

    const qreal space = metrics.horizontalAdvance(u' ');
    const qreal gap = metrics.horizontalAdvance(QStringLiteral("MM"));
    const auto widthOf = [&](QChar ch) {
        if (ch == QChar::ParagraphSeparator)
            return gap;
        if (ch == u'\t')
            return space * 4;
        return metrics.horizontalAdvance(ch);
    };

    const qreal end = maxTheta();
    int page = 0;
    qreal theta = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        // A word that would run past the centre starts the next page instead.
        if (!isBreak(ch) && (i == 0 || isBreak(text.at(i - 1))) && theta > 0) {
            qreal span = 0;
            for (int j = i; j < text.size() && !isBreak(text.at(j)); ++j)
                span += widthOf(text.at(j)) / radiusAt(theta + span);
            if (theta + span > end && span < end) {
                ++page;
                theta = 0;
            }
        }
        const qreal width = widthOf(ch);
        qreal advance = width / radiusAt(theta);
        if (theta + advance > end && theta > 0) {
            ++page;
            theta = 0;
            advance = width / radiusAt(theta);
        }
        Glyph glyph;
        glyph.gap = ch == QChar::ParagraphSeparator;
        if (!glyph.gap && ch != u'\t')
            glyph.text = QString(ch);
        glyph.advance = advance;
        m_stops.append({ page, theta });
        m_glyphs.append(glyph);
        theta += advance;
    }
    m_stops.append({ page, theta });
    m_pages = page + 1;
}

int SpiralLayout::pageOf(int position) const
{
    if (m_stops.isEmpty())
        return 0;
    return m_stops.at(qBound(0, position, positionCount())).page;
}

int SpiralLayout::turnOf(int position) const
{
    if (m_stops.isEmpty())
        return 0;
    return int(std::floor(m_stops.at(qBound(0, position, positionCount())).theta / kTurn));
}

qreal SpiralLayout::angleOf(int position) const
{
    if (m_stops.isEmpty())
        return 0;
    return std::fmod(m_stops.at(qBound(0, position, positionCount())).theta, kTurn);
}

int SpiralLayout::firstOf(int page) const
{
    for (int i = 0; i < m_stops.size(); ++i) {
        if (m_stops.at(i).page == page)
            return i;
    }
    return 0;
}

int SpiralLayout::lastOf(int page) const
{
    for (int i = int(m_stops.size()) - 1; i >= 0; --i) {
        if (m_stops.at(i).page == page)
            return i;
    }
    return 0;
}

int SpiralLayout::turnCount(int page) const
{
    return turnOf(lastOf(page)) + 1;
}

int SpiralLayout::positionOnTurn(int page, int turn, qreal angle) const
{
    const qreal target = angle + turn * kTurn;
    int best = firstOf(page);
    qreal bestDistance = -1;
    for (int i = firstOf(page), last = lastOf(page); i <= last; ++i) {
        const qreal d = std::abs(m_stops.at(i).theta - target);
        if (bestDistance < 0 || d < bestDistance) {
            best = i;
            bestDistance = d;
        }
    }
    return best;
}

int SpiralLayout::turnStart(int page, int turn) const
{
    for (int i = firstOf(page), last = lastOf(page); i <= last; ++i) {
        if (m_stops.at(i).theta >= turn * kTurn)
            return i;
    }
    return lastOf(page);
}

int SpiralLayout::turnEnd(int page, int turn) const
{
    int end = firstOf(page);
    for (int i = firstOf(page), last = lastOf(page); i <= last; ++i) {
        if (m_stops.at(i).theta < (turn + 1) * kTurn)
            end = i;
    }
    // At the end of a turn that the text carries on past, the cursor sits
    // before the last glyph's neighbour rather than on the next turn.
    return end;
}

int SpiralLayout::positionAt(int page, const QPointF &point) const
{
    const qreal r = m_diameter / 2;
    const QPointF v = point - QPointF(r, r);
    qreal angle = std::atan2(v.y(), v.x()) + std::numbers::pi / 2;
    if (angle < 0)
        angle += kTurn;
    const qreal distance = std::hypot(v.x(), v.y());
    // radiusAt(angle + turn * kTurn) == distance, solved for the turn.
    const int turn = qBound(0, int(std::lround((m_outerRadius - distance) / m_pitch - angle / kTurn)),
                            turnCount(page) - 1);
    return positionOnTurn(page, turn, angle);
}

QLineF SpiralLayout::caret(int position) const
{
    if (m_stops.isEmpty())
        return {};
    const qreal theta = m_stops.at(qBound(0, position, positionCount())).theta;
    return QLineF(pointAt(theta, -m_pitch * 0.45), pointAt(theta, m_pitch * 0.45));
}

void SpiralLayout::drawGroove(QPainter &p, const QColor &color) const
{
    if (m_pitch <= 0)
        return;
    QPolygonF groove;
    const qreal end = maxTheta();
    for (qreal theta = 0; theta <= end; theta += 0.04)
        groove.append(pointAt(theta, -m_pitch / 2));
    groove.append(pointAt(end, -m_pitch / 2));
    p.save();
    p.setPen(QPen(color, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(groove);
    p.restore();
}

void SpiralLayout::draw(QPainter &p, int page, const QColor &ink, const QColor &selection, const QColor &mark,
                        int selectionStart, int selectionEnd) const
{
    p.save();
    p.setFont(m_font);
    const qreal h = m_pitch / 2;
    for (int i = 0; i < m_glyphs.size(); ++i) {
        const Stop &stop = m_stops.at(i);
        if (stop.page != page)
            continue;
        const Glyph &glyph = m_glyphs.at(i);
        const qreal theta = stop.theta;
        const qreal mid = theta + glyph.advance / 2;

        if (i >= selectionStart && i < selectionEnd) {
            const QPolygonF band({ pointAt(theta, -h), pointAt(theta, h), pointAt(mid, h),
                                   pointAt(theta + glyph.advance, h), pointAt(theta + glyph.advance, -h),
                                   pointAt(mid, -h) });
            p.setPen(Qt::NoPen);
            p.setBrush(selection);
            p.drawPolygon(band);
        }
        if (glyph.gap) {
            p.setPen(Qt::NoPen);
            p.setBrush(mark);
            p.drawEllipse(pointAt(mid), m_pitch * 0.08, m_pitch * 0.08);
            continue;
        }
        if (glyph.text.isEmpty())
            continue;
        // Each letter stands on the groove, its head towards the rim.
        const qreal width = glyph.advance * radiusAt(mid);
        p.save();
        p.translate(pointAt(mid));
        p.rotate((-std::numbers::pi / 2 + mid) * 180 / std::numbers::pi + 90);
        p.setPen(ink);
        p.drawText(QPointF(-width / 2, m_baseline), glyph.text);
        p.restore();
    }
    p.restore();
}
