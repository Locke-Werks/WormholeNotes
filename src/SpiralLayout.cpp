#include "SpiralLayout.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPolygonF>
#include <QTextBlock>
#include <QTextDocument>

#include <cmath>
#include <numbers>

namespace {

constexpr qreal kPi = std::numbers::pi;
constexpr qreal kTurn = 2 * kPi;
// Inset of the outer turn from the rim, as a share of the radius.
constexpr qreal kInset = 0.05;
// The groove stops this far out, as a share of the radius, leaving the
// centre for the page controls.
constexpr qreal kCentre = 0.30;
// Where the newest writing ends: a little past noon, so the word being typed
// sits across the top of the circle.
constexpr qreal kAnchor = 0.30;

bool isBreak(QChar ch)
{
    return ch.isSpace() || ch == QChar::ParagraphSeparator;
}

qreal wrap(qreal angle)
{
    angle = std::fmod(angle, kTurn);
    return angle < 0 ? angle + kTurn : angle;
}

} // namespace

qreal SpiralLayout::anchor()
{
    return kAnchor;
}

void SpiralLayout::setFont(const QFont &font)
{
    m_font = font;
}

void SpiralLayout::setDiameter(qreal diameter)
{
    m_diameter = diameter;
}

qreal SpiralLayout::radiusAt(qreal t) const
{
    return m_outerRadius - m_pitch * t / kTurn;
}

QPointF SpiralLayout::pointAt(qreal t, qreal offset) const
{
    const qreal r = m_diameter / 2;
    const qreal screen = -kPi / 2 + kAnchor - t;
    return QPointF(r, r) + QPointF(std::cos(screen), std::sin(screen)) * (radiusAt(t) + offset);
}

qreal SpiralLayout::maxT() const
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

    // Laid out backwards from the newest end, so it is the newest writing
    // that is pinned at the top of the outer turn. Pages are counted
    // backwards too, then numbered oldest first.
    const qsizetype n = text.size();
    m_stops.resize(n + 1);
    m_glyphs.resize(n);
    const qreal end = maxT();
    int back = 0;
    qreal t = 0;
    m_stops[n] = { 0, 0 };
    for (qsizetype i = n - 1; i >= 0; --i) {
        const QChar ch = text.at(i);
        // Meeting a word at its end: if the whole word would run past the
        // centre, it goes onto the page before instead.
        if (!isBreak(ch) && (i == n - 1 || isBreak(text.at(i + 1))) && t > 0) {
            qreal span = 0;
            for (qsizetype j = i; j >= 0 && !isBreak(text.at(j)); --j)
                span += widthOf(text.at(j)) / radiusAt(t + span);
            if (t + span > end && span < end) {
                ++back;
                t = 0;
            }
        }
        const qreal width = widthOf(ch);
        qreal advance = width / radiusAt(t);
        if (t + advance > end && t > 0) {
            ++back;
            t = 0;
            advance = width / radiusAt(t);
        }
        Glyph glyph;
        glyph.gap = ch == QChar::ParagraphSeparator;
        if (!glyph.gap && ch != u'\t')
            glyph.text = QString(ch);
        glyph.advance = advance;
        m_glyphs[i] = glyph;
        t += advance;
        m_stops[i] = { back, t };
    }
    m_pages = back + 1;
    for (Stop &stop : m_stops)
        stop.page = m_pages - 1 - stop.page;
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
    return int(std::floor(m_stops.at(qBound(0, position, positionCount())).t / kTurn));
}

qreal SpiralLayout::screenAngle(int position) const
{
    if (m_stops.isEmpty())
        return kAnchor;
    return kAnchor - m_stops.at(qBound(0, position, positionCount())).t;
}

qreal SpiralLayout::angleOf(int position) const
{
    return wrap(screenAngle(position));
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
    // The oldest writing on a page is the furthest in.
    return turnOf(firstOf(page)) + 1;
}

int SpiralLayout::positionOnTurn(int page, int turn, qreal angle) const
{
    const qreal target = turn * kTurn + wrap(kAnchor - angle);
    int best = lastOf(page);
    qreal bestDistance = -1;
    for (int i = firstOf(page), last = lastOf(page); i <= last; ++i) {
        const qreal d = std::abs(m_stops.at(i).t - target);
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
        if (turnOf(i) == turn)
            return i;
    }
    return firstOf(page);
}

int SpiralLayout::turnEnd(int page, int turn) const
{
    for (int i = lastOf(page), first = firstOf(page); i >= first; --i) {
        if (turnOf(i) == turn)
            return i;
    }
    return lastOf(page);
}

int SpiralLayout::positionAt(int page, const QPointF &point) const
{
    const qreal r = m_diameter / 2;
    const QPointF v = point - QPointF(r, r);
    const qreal angle = wrap(std::atan2(v.y(), v.x()) + kPi / 2);
    const qreal distance = std::hypot(v.x(), v.y());
    const qreal base = wrap(kAnchor - angle);
    // radiusAt(base + turn * kTurn) == distance, solved for the turn.
    const int turn =
        qBound(0, int(std::lround((m_outerRadius - distance) / m_pitch - base / kTurn)), turnCount(page) - 1);
    return positionOnTurn(page, turn, angle);
}

QLineF SpiralLayout::caret(int position) const
{
    if (m_stops.isEmpty())
        return {};
    const qreal t = m_stops.at(qBound(0, position, positionCount())).t;
    return QLineF(pointAt(t, -m_pitch * 0.45), pointAt(t, m_pitch * 0.45));
}

void SpiralLayout::drawGroove(QPainter &p, const QColor &color) const
{
    if (m_pitch <= 0)
        return;
    QPolygonF groove;
    const qreal end = maxT();
    for (qreal t = 0; t <= end; t += 0.04)
        groove.append(pointAt(t, -m_pitch / 2));
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
        // The glyph runs from its older side, at the stop, to its newer side,
        // an advance nearer the newest end.
        const qreal older = stop.t;
        const qreal newer = older - glyph.advance;
        const qreal mid = older - glyph.advance / 2;

        if (i >= selectionStart && i < selectionEnd) {
            const QPolygonF band({ pointAt(older, -h), pointAt(older, h), pointAt(mid, h), pointAt(newer, h),
                                   pointAt(newer, -h), pointAt(mid, -h) });
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
        p.rotate((-kPi / 2 + kAnchor - mid) * 180 / kPi + 90);
        p.setPen(ink);
        p.drawText(QPointF(-width / 2, m_baseline), glyph.text);
        p.restore();
    }
    p.restore();
}
