#pragma once

#include <QFont>
#include <QList>
#include <QLineF>
#include <QPointF>
#include <QString>

class QPainter;
class QTextDocument;

// Lays a document out along a spiral groove, anchored at its newest end:
// the last thing written sits at the top of the outer turn, upright, and
// everything before it runs back along the groove, anticlockwise and inward,
// towards the centre. Read clockwise from the middle outward, it is in order.
// As writing goes on, what came before slides further in, so the words being
// typed are always the right way up at the top.
//
// A new paragraph does not break the groove; it leaves a short gap with a dot
// in it. When the groove is full down to the centre, the oldest writing moves
// onto the page before. A word is never split between pages. Page 0 is the
// oldest; the last page holds the newest writing.
//
// Coordinates are the face's own: the circle's bounding square starts at
// (0, 0). Distance along the groove, t, is an angle measured from the newest
// end inward, so each turn of the groove has its own range of t.
class SpiralLayout
{
public:
    void setFont(const QFont &font);
    void setDiameter(qreal diameter);
    qreal diameter() const { return m_diameter; }
    void layout(const QTextDocument *document);

    int pageCount() const { return m_pages; }
    // Where the groove stops: inside this radius is the centre, left free.
    qreal hubRadius() const { return m_innerRadius - m_pitch * 0.6; }
    qreal lineHeight() const { return m_pitch; }

    // Cursor positions run 0..positionCount(); each sits before the glyph of
    // the same number.
    int positionCount() const { return int(m_stops.size()) - 1; }
    int pageOf(int position) const;
    int turnOf(int position) const; // which turn of its page, 0 at the rim
    int turnCount(int page) const;
    qreal angleOf(int position) const; // around the circle, 0 .. 2*pi
    int firstOf(int page) const;
    int lastOf(int page) const;
    // The position on a turn of a page nearest an angle around the circle.
    int positionOnTurn(int page, int turn, qreal angle) const;
    int turnStart(int page, int turn) const;
    int turnEnd(int page, int turn) const;
    int positionAt(int page, const QPointF &point) const;
    // The angle, clockwise from noon, where a position sits on the face.
    qreal screenAngle(int position) const;
    // The angle the newest end of the writing sits at, clockwise from noon.
    static qreal anchor();

    // The caret: a short stroke across the groove.
    QLineF caret(int position) const;
    // The groove itself, faint, for the page's guide.
    void drawGroove(QPainter &p, const QColor &color) const;
    void draw(QPainter &p, int page, const QColor &ink, const QColor &selection, const QColor &mark, int selectionStart,
              int selectionEnd) const;

private:
    struct Stop
    {
        int page = 0;
        qreal t = 0; // along the groove from its newest end
    };
    struct Glyph
    {
        QString text;      // empty for a paragraph gap or a tab
        qreal advance = 0; // angle it takes along the groove
        bool gap = false;
    };

    qreal radiusAt(qreal t) const;
    QPointF pointAt(qreal t, qreal offset = 0) const;
    qreal maxT() const;

    QFont m_font;
    qreal m_diameter = 0;
    qreal m_outerRadius = 0;
    qreal m_innerRadius = 0;
    qreal m_pitch = 0;
    qreal m_baseline = 0;
    int m_pages = 1;
    // Stop i sits at the start (the older side) of glyph i; the last stop is
    // the newest end, t = 0 on the last page.
    QList<Stop> m_stops;
    QList<Glyph> m_glyphs;
};
