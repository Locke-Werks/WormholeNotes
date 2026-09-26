#include "RingTextLayout.h"

#include "Arc.h"

#include <QFontMetricsF>
#include <QGlyphRun>
#include <QPainter>
#include <QRawFont>
#include <QTextDocument>
#include <QTextLayout>

#include <cmath>

namespace {

// Rings are spaced a little looser than lines on paper: curved baselines read
// as more crowded than straight ones at the same spacing.
constexpr qreal kPitchFactor = 1.14;
constexpr qreal kEdgePad = 6;

} // namespace

RingTextLayout::RingTextLayout(QTextDocument *document)
    : QAbstractTextDocumentLayout(document)
{
}

void RingTextLayout::setDisc(qreal outerRadius, qreal hubRadius)
{
    if (qAbs(outerRadius - m_outer) < 0.01 && qAbs(hubRadius - m_hub) < 0.01)
        return;
    m_outer = outerRadius;
    m_hub = hubRadius;
    m_fontKey.clear(); // forces the geometry to be rebuilt
    relayout(0, document()->characterCount());
}

void RingTextLayout::computeGeometry()
{
    const QFont font = document()->defaultFont();
    const QFontMetricsF metrics(font);
    m_ascent = metrics.ascent();
    m_descent = metrics.descent();
    m_pitch = std::ceil(metrics.lineSpacing() * kPitchFactor);
    m_fontKey = font.key();

    // The gap at noon is where every line starts and ends. It is a fixed width
    // rather than a fixed angle, so it stays the same size on every ring.
    const qreal gap = qMax(metrics.averageCharWidth() * 2.5, 16.0);

    m_geometry.clear();
    for (int i = 0;; ++i) {
        const qreal baseline = m_outer - kEdgePad - m_ascent - i * m_pitch;
        if (i > 0 && baseline - m_descent < m_hub + kEdgePad)
            break;
        Ring ring;
        ring.baseline = baseline;
        ring.mid = qMax<qreal>(1, baseline + metrics.xHeight() / 2);
        ring.sweep = qMax(Arc::kPi, Arc::kTau - gap / ring.mid);
        ring.start = Arc::kNoon + (Arc::kTau - ring.sweep) / 2;
        ring.width = ring.sweep * ring.mid;
        m_geometry.append(ring);
    }
    m_ringsPerPage = int(m_geometry.size());
}

void RingTextLayout::documentChanged(int from, int /*charsRemoved*/, int charsAdded)
{
    relayout(from, from + charsAdded);
}

int RingTextLayout::firstRing(const QTextBlock &block) const
{
    return qRound(block.layout()->position().y() / m_pitch);
}

int RingTextLayout::endRing(const QTextBlock &block) const
{
    return firstRing(block) + block.layout()->lineCount();
}

void RingTextLayout::relayout(int from, int changeEnd)
{
    if (m_outer <= 0)
        return;
    QTextDocument *doc = document();
    const QSizeF oldSize = documentSize();
    const int oldPages = m_pages;

    if (doc->defaultFont().key() != m_fontKey) {
        computeGeometry();
        from = 0;
        changeEnd = doc->characterCount();
    }

    QTextBlock block = doc->findBlock(from);
    if (!block.isValid())
        block = doc->begin();
    int ring = block.previous().isValid() ? endRing(block.previous()) : 0;

    for (; block.isValid(); block = block.next()) {
        const QTextLayout *existing = block.layout();
        if (block.position() > changeEnd && existing->lineCount() > 0 && firstRing(block) == ring)
            break;
        ring = layoutBlock(block, ring);
    }

    m_rings = qMax(1, endRing(doc->lastBlock()));
    m_pages = (m_rings + m_ringsPerPage - 1) / m_ringsPerPage;

    const QSizeF newSize = documentSize();
    if (newSize != oldSize)
        emit documentSizeChanged(newSize);
    if (m_pages != oldPages)
        emit pageCountChanged(m_pages);
    emit update();
}

int RingTextLayout::layoutBlock(const QTextBlock &block, int first)
{
    QTextLayout *layout = block.layout();
    QTextOption option = document()->defaultTextOption();
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setTextDirection(Qt::LeftToRight);
    layout->setTextOption(option);
    layout->setCacheEnabled(true);
    layout->setPosition(QPointF(0, first * m_pitch));

    int count = 0;
    layout->beginLayout();
    for (;;) {
        QTextLine line = layout->createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(ring(first + count).width);
        line.setPosition(QPointF(0, count * m_pitch));
        ++count;
    }
    layout->endLayout();
    return first + count;
}

RingTextLayout::LineRef RingTextLayout::lineAtRing(int ringIndex) const
{
    const QTextDocument *doc = document();
    if (m_outer <= 0 || ringIndex < 0 || ringIndex >= m_rings)
        return {};
    int lo = 0;
    int hi = doc->blockCount() - 1;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (firstRing(doc->findBlockByNumber(mid)) <= ringIndex)
            lo = mid;
        else
            hi = mid - 1;
    }
    const QTextBlock block = doc->findBlockByNumber(lo);
    const int index = ringIndex - firstRing(block);
    if (index < 0 || index >= block.layout()->lineCount())
        return {};
    return { block, index };
}

int RingTextLayout::ringOfPosition(int position, qreal *x) const
{
    if (x)
        *x = 0;
    if (m_outer <= 0)
        return 0;
    const QTextBlock block = document()->findBlock(position);
    if (!block.isValid())
        return m_rings - 1;
    const QTextLayout *layout = block.layout();
    if (layout->lineCount() == 0)
        return firstRing(block);
    const int relative = position - block.position();
    QTextLine line = layout->lineForTextPosition(relative);
    if (!line.isValid())
        line = layout->lineAt(layout->lineCount() - 1);
    if (x)
        *x = line.cursorToX(relative);
    return firstRing(block) + line.lineNumber();
}

qreal RingTextLayout::angleOfPosition(int position) const
{
    qreal x = 0;
    const int r = ringOfPosition(position, &x);
    const Ring &g = ring(r);
    return g.start + x / g.mid;
}

int RingTextLayout::positionOnRing(int ringIndex, qreal angle) const
{
    if (m_outer <= 0)
        return 0;
    ringIndex = qBound(0, ringIndex, m_rings - 1);
    const LineRef ref = lineAtRing(ringIndex);
    if (!ref.isValid())
        return document()->characterCount() - 1;

    const Ring &g = ring(ringIndex);
    const qreal along = Arc::wrapPositive(angle - g.start);
    qreal x;
    if (along <= g.sweep) {
        x = along * g.mid;
    } else {
        // In the gap at noon: whichever end of the line is nearer.
        const qreal pastEnd = along - g.sweep;
        x = pastEnd < (Arc::kTau - g.sweep) / 2 ? g.width : 0;
    }
    const QTextLine line = ref.block.layout()->lineAt(ref.index);
    return ref.block.position() + line.xToCursor(x);
}

int RingTextLayout::positionAt(int page, const QPointF &point) const
{
    if (m_outer <= 0)
        return 0;
    const qreal distance = std::hypot(point.x(), point.y());
    const qreal angle = std::atan2(point.y(), point.x());

    // The ring whose band of ink is nearest the point.
    int best = 0;
    qreal bestDistance = 1e9;
    for (int i = 0; i < m_ringsPerPage; ++i) {
        const qreal centre = m_geometry.at(i).baseline + (m_ascent - m_descent) / 2;
        const qreal d = qAbs(distance - centre);
        if (d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    int ringIndex = page * m_ringsPerPage + best;
    // Past the end of the text: the last line, at the angle clicked, the way a
    // click below the last line of Notepad lands on the last line.
    if (ringIndex >= m_rings)
        ringIndex = m_rings - 1;
    return positionOnRing(ringIndex, angle);
}

QPointF RingTextLayout::glyphPoint(const Ring &g, qreal radius, qreal x) const
{
    return Arc::polar(QPointF(0, 0), radius, g.start + x / g.mid);
}

void RingTextLayout::paintGuides(QPainter *painter, const QColor &color) const
{
    if (m_outer <= 0)
        return;
    painter->save();
    painter->setPen(QPen(color, 1));
    painter->setBrush(Qt::NoBrush);
    for (const Ring &g : m_geometry)
        painter->drawEllipse(QPointF(0, 0), g.baseline, g.baseline);
    // The seam at noon, where every line begins.
    const Ring &outer = m_geometry.first();
    const Ring &inner = m_geometry.last();
    painter->drawLine(Arc::polar(QPointF(0, 0), inner.baseline - m_descent, Arc::kNoon),
                      Arc::polar(QPointF(0, 0), outer.baseline + m_ascent, Arc::kNoon));
    painter->restore();
}

void RingTextLayout::paintPage(QPainter *painter, int page, const QColor &ink, const QColor &selection,
                               int selectionStart, int selectionEnd) const
{
    if (m_outer <= 0)
        return;
    const int first = page * m_ringsPerPage;
    const int last = qMin(m_rings, first + m_ringsPerPage) - 1;
    if (first > last)
        return;

    painter->save();
    painter->setPen(ink);

    LineRef ref = lineAtRing(first);
    for (int r = first; r <= last && ref.isValid(); ++r) {
        const Ring &g = ring(r);
        const QTextLayout *layout = ref.block.layout();
        const QTextLine line = layout->lineAt(ref.index);
        const int blockPos = ref.block.position();
        const int lineStart = blockPos + line.textStart();
        const int lineEnd = lineStart + line.textLength();
        const bool lastInBlock = ref.index == layout->lineCount() - 1;

        if (selectionStart < selectionEnd) {
            // A selection that runs over the end of a paragraph shows a sliver
            // past the last character, the way a selected newline does in Notepad.
            const int s = qMax(selectionStart, lineStart);
            const int e = qMin(selectionEnd, lineEnd + (lastInBlock ? 1 : 0));
            if (s < e) {
                const qreal x0 = line.cursorToX(s - blockPos);
                qreal x1 = line.cursorToX(qMin(e, lineEnd) - blockPos);
                if (e > lineEnd)
                    x1 = qMin(x1 + m_ascent * 0.4, g.width);
                const QPainterPath band = Arc::sector(QPointF(0, 0), g.baseline - m_descent,
                                                      g.baseline + m_ascent * 0.9,
                                                      g.start + x0 / g.mid, (x1 - x0) / g.mid);
                painter->fillPath(band, selection);
            }
        }

        // One glyph at a time, each turned to the tangent at its own centre.
        const QList<QGlyphRun> runs = line.glyphRuns();
        for (const QGlyphRun &run : runs) {
            const QRawFont font = run.rawFont();
            const QList<quint32> glyphs = run.glyphIndexes();
            const QList<QPointF> positions = run.positions();
            const QList<QPointF> advances = font.advancesForGlyphIndexes(glyphs);
            for (qsizetype i = 0; i < glyphs.size(); ++i) {
                const qreal advance = advances.value(i).x();
                const qreal centreX = positions.at(i).x() + advance / 2;
                const qreal angle = g.start + centreX / g.mid;
                QGlyphRun one;
                one.setRawFont(font);
                one.setGlyphIndexes({ glyphs.at(i) });
                one.setPositions({ QPointF(-advance / 2, 0) });
                painter->save();
                painter->translate(Arc::polar(QPointF(0, 0), g.baseline, angle));
                painter->rotate(Arc::degrees(angle) + 90);
                painter->drawGlyphRun(QPointF(0, 0), one);
                painter->restore();
            }
        }

        if (ref.index + 1 < layout->lineCount()) {
            ++ref.index;
        } else {
            ref.block = ref.block.next();
            ref.index = 0;
            if (!ref.block.isValid())
                break;
        }
    }
    painter->restore();
}

void RingTextLayout::paintCursor(QPainter *painter, int page, int position, const QColor &color,
                                 qreal width) const
{
    if (m_outer <= 0)
        return;
    qreal x = 0;
    const int r = ringOfPosition(position, &x);
    if (pageOfRing(r) != page)
        return;
    const Ring &g = ring(r);
    painter->save();
    painter->setPen(QPen(color, width, Qt::SolidLine, Qt::FlatCap));
    painter->drawLine(glyphPoint(g, g.baseline - m_descent, x), glyphPoint(g, g.baseline + m_ascent, x));
    painter->restore();
}

void RingTextLayout::draw(QPainter *, const PaintContext &)
{
    // RoundEdit paints through paintPage(); nothing draws through this path.
}

int RingTextLayout::hitTest(const QPointF &, Qt::HitTestAccuracy) const
{
    return -1;
}

QSizeF RingTextLayout::documentSize() const
{
    return QSizeF(2 * m_outer, m_rings * m_pitch);
}

QRectF RingTextLayout::frameBoundingRect(QTextFrame *) const
{
    return QRectF(QPointF(0, 0), documentSize());
}

QRectF RingTextLayout::blockBoundingRect(const QTextBlock &block) const
{
    if (!block.isValid() || m_outer <= 0)
        return {};
    const QTextLayout *layout = block.layout();
    return QRectF(0, layout->position().y(), 2 * m_outer, layout->lineCount() * m_pitch);
}
