#include "CircleLayout.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include <cmath>

namespace {

// Inset from the rim, as a share of the radius, so letters never touch it.
constexpr qreal kInset = 0.07;
// The writing band: from here near the top to here below the middle, as
// shares of the radius from the centre. The top is where a line gets too
// short to hold a word; the bottom leaves room for the page controls.
constexpr qreal kTop = 0.80;
constexpr qreal kBottom = 0.62;

} // namespace

CircleLayout::CircleLayout()
{
    m_option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
}

CircleLayout::~CircleLayout() = default;

void CircleLayout::setFont(const QFont &font)
{
    m_font = font;
}

void CircleLayout::setTabStop(qreal distance)
{
    m_option.setTabStopDistance(distance);
}

void CircleLayout::setDiameter(qreal diameter)
{
    m_diameter = diameter;
}

// How wide the circle is across a line whose top is at y, measured at the
// line's edge nearer the rim, so no letter pokes out of the circle.
qreal CircleLayout::chord(qreal y) const
{
    const qreal r = m_diameter / 2;
    const qreal inner = r * (1 - kInset);
    const qreal dy = qMax(std::abs(y - r), std::abs(y + m_lineHeight - r));
    return dy >= inner ? 0 : 2 * std::sqrt(inner * inner - dy * dy);
}

void CircleLayout::layout(const QTextDocument *document)
{
    m_blocks.clear();
    m_blockStart.clear();
    m_lineOf.clear();
    m_lines.clear();
    m_pages = 1;

    const QFontMetricsF metrics(m_font);
    m_lineHeight = metrics.lineSpacing();
    const qreal r = m_diameter / 2;
    m_top = r - r * kTop;
    m_bottom = r + r * kBottom;
    if (m_diameter <= 0)
        return;

    int page = 0;
    qreal y = m_top;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        auto layout = std::make_unique<QTextLayout>(block.text(), m_font);
        layout->setTextOption(m_option);
        layout->setCacheEnabled(true);
        QList<int> lineIndices;
        layout->beginLayout();
        for (;;) {
            QTextLine line = layout->createLine();
            if (!line.isValid())
                break;
            if (y + m_lineHeight > m_bottom) {
                ++page;
                y = m_top;
            }
            const qreal width = qMax(m_lineHeight, chord(y));
            line.setLineWidth(width);
            line.setPosition(QPointF(r - width / 2, y));
            lineIndices.append(int(m_lines.size()));
            m_lines.append({ int(m_blocks.size()), line.lineNumber(), page,
                             QRectF(r - width / 2, y, width, m_lineHeight) });
            y += m_lineHeight;
        }
        layout->endLayout();
        m_blockStart.append(block.position());
        m_lineOf.append(lineIndices);
        m_blocks.push_back(std::move(layout));
    }
    m_pages = page + 1;
}

int CircleLayout::lineAt(int position) const
{
    int block = 0;
    while (block + 1 < m_blockStart.size() && m_blockStart.at(block + 1) <= position)
        ++block;
    if (block >= int(m_blocks.size()))
        return -1;
    const int rel = position - m_blockStart.at(block);
    const QTextLine line = m_blocks[block]->lineForTextPosition(rel);
    const QList<int> &indices = m_lineOf.at(block);
    if (indices.isEmpty())
        return -1;
    return line.isValid() ? indices.at(line.lineNumber()) : indices.last();
}

int CircleLayout::pageOf(int position) const
{
    const int line = lineAt(position);
    return line < 0 ? 0 : m_lines.at(line).page;
}

qreal CircleLayout::cursorX(int position) const
{
    const int index = lineAt(position);
    if (index < 0)
        return m_diameter / 2;
    const Line &line = m_lines.at(index);
    const QTextLine textLine = m_blocks[line.block]->lineAt(line.index);
    return textLine.cursorToX(position - m_blockStart.at(line.block));
}

QRectF CircleLayout::cursorRect(int position) const
{
    const int index = lineAt(position);
    if (index < 0)
        return QRectF(m_diameter / 2, m_top, 1, m_lineHeight);
    const Line &line = m_lines.at(index);
    return QRectF(cursorX(position), line.rect.top(), 1, line.rect.height());
}

int CircleLayout::positionIn(int index, qreal x) const
{
    if (index < 0 || index >= m_lines.size())
        return 0;
    const Line &line = m_lines.at(index);
    const QTextLine textLine = m_blocks[line.block]->lineAt(line.index);
    return m_blockStart.at(line.block) + textLine.xToCursor(x);
}

int CircleLayout::positionAt(int page, const QPointF &point) const
{
    int best = -1;
    qreal bestDistance = 0;
    for (int i = 0; i < m_lines.size(); ++i) {
        const Line &line = m_lines.at(i);
        if (line.page != page)
            continue;
        const qreal d = point.y() < line.rect.top() ? line.rect.top() - point.y()
            : point.y() > line.rect.bottom()        ? point.y() - line.rect.bottom()
                                                    : 0;
        if (best < 0 || d < bestDistance) {
            best = i;
            bestDistance = d;
        }
    }
    return best < 0 ? 0 : positionIn(best, point.x());
}

int CircleLayout::lineStart(int index) const
{
    const Line &line = m_lines.at(index);
    return m_blockStart.at(line.block) + m_blocks[line.block]->lineAt(line.index).textStart();
}

int CircleLayout::lineEnd(int index) const
{
    const Line &line = m_lines.at(index);
    const QTextLine textLine = m_blocks[line.block]->lineAt(line.index);
    int end = textLine.textStart() + textLine.textLength();
    // A wrapped line ends before the space it broke at, so End stays on it.
    const QString text = m_blocks[line.block]->text();
    const bool lastInBlock = line.index == m_blocks[line.block]->lineCount() - 1;
    if (!lastInBlock && end > textLine.textStart() && text.at(end - 1).isSpace())
        --end;
    return m_blockStart.at(line.block) + end;
}

void CircleLayout::draw(QPainter &p, int page, const QColor &ink, const QColor &selection, int selectionStart,
                        int selectionEnd) const
{
    p.save();
    p.setPen(ink);
    for (const Line &line : m_lines) {
        if (line.page != page)
            continue;
        const QTextLine textLine = m_blocks[line.block]->lineAt(line.index);
        const int start = m_blockStart.at(line.block) + textLine.textStart();
        const int end = start + textLine.textLength();
        if (selectionEnd > selectionStart && selectionStart < end + 1 && selectionEnd > start) {
            const qreal x0 = textLine.cursorToX(qMax(selectionStart, start) - m_blockStart.at(line.block));
            qreal x1 = textLine.cursorToX(qMin(selectionEnd, end) - m_blockStart.at(line.block));
            // A selection running on past the end of the line shows the break.
            if (selectionEnd > end)
                x1 += QFontMetricsF(m_font).horizontalAdvance(u' ');
            p.fillRect(QRectF(QPointF(x0, line.rect.top()), QPointF(x1, line.rect.bottom())), selection);
        }
        textLine.draw(&p, QPointF(0, 0));
    }
    p.restore();
}
