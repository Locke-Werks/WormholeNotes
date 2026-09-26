#pragma once

#include <QFont>
#include <QList>
#include <QRectF>
#include <QTextOption>

#include <memory>
#include <vector>

class QPainter;
class QTextDocument;
class QTextLayout;

// Lays a document out inside a circle the way writing fills a round card:
// ordinary horizontal lines, each as wide as the circle is at that height.
// When the lines reach the foot of the circle the text carries on onto the
// next page, which starts again at the top.
//
// Coordinates are the face's own: the circle's bounding square starts at
// (0, 0) and every page uses the same space.
class CircleLayout
{
public:
    struct Line
    {
        int block = 0;
        int index = 0; // line within its block's layout
        int page = 0;
        QRectF rect;
    };

    CircleLayout();
    ~CircleLayout();

    void setFont(const QFont &font);
    void setTabStop(qreal distance);
    void setDiameter(qreal diameter);
    qreal diameter() const { return m_diameter; }
    void layout(const QTextDocument *document);

    int pageCount() const { return m_pages; }
    const QList<Line> &lines() const { return m_lines; }
    // The band the text is written in, top and bottom. Below it is room for
    // the page controls.
    qreal top() const { return m_top; }
    qreal bottom() const { return m_bottom; }
    qreal lineHeight() const { return m_lineHeight; }

    int lineAt(int position) const; // index into lines()
    int pageOf(int position) const;
    QRectF cursorRect(int position) const;
    qreal cursorX(int position) const;
    int positionIn(int line, qreal x) const;
    int positionAt(int page, const QPointF &point) const;
    int lineStart(int line) const;
    int lineEnd(int line) const;

    void draw(QPainter &p, int page, const QColor &ink, const QColor &selection, int selectionStart,
              int selectionEnd) const;

private:
    qreal chord(qreal y) const;

    QFont m_font;
    QTextOption m_option;
    qreal m_diameter = 0;
    qreal m_top = 0;
    qreal m_bottom = 0;
    qreal m_lineHeight = 0;
    int m_pages = 1;
    std::vector<std::unique_ptr<QTextLayout>> m_blocks;
    QList<int> m_blockStart;
    QList<QList<int>> m_lineOf; // block, line within block -> index into m_lines
    QList<Line> m_lines;
};
