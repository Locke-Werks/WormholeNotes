#pragma once

#include <QFont>
#include <QList>
#include <QLineF>
#include <QPointF>
#include <QString>

class QPainter;
class QTextDocument;

// Lays a document out along a spiral, the way a record's groove runs: one
// unbroken line that starts at the rim at noon, winds clockwise and closes in
// on the centre. A new paragraph does not break the line; it leaves a short
// gap with a dot in it. When the spiral reaches the centre the page is full
// and the writing carries on from the rim of the next page. A word is never
// split between pages.
//
// Coordinates are the face's own: the circle's bounding square starts at
// (0, 0). Angles along the spiral are measured from noon, clockwise, and keep
// growing past a full turn, so each turn of the groove has its own range.
class SpiralLayout
{
public:
    void setFont(const QFont &font);
    void setDiameter(qreal diameter);
    qreal diameter() const { return m_diameter; }
    void layout(const QTextDocument *document);

    int pageCount() const { return m_pages; }
    // Where the spiral stops: inside this radius is the centre, left free.
    qreal hubRadius() const { return m_innerRadius - m_pitch * 0.6; }
    qreal lineHeight() const { return m_pitch; }

    // Cursor positions run 0..positionCount(); each sits before the glyph of
    // the same number.
    int positionCount() const { return int(m_stops.size()) - 1; }
    int pageOf(int position) const;
    int turnOf(int position) const; // which turn of its page, 0 at the rim
    int turnCount(int page) const;
    qreal angleOf(int position) const; // within its turn, 0 .. 2*pi
    int firstOf(int page) const;
    int lastOf(int page) const;
    // The position on a turn of a page nearest an angle within the turn.
    int positionOnTurn(int page, int turn, qreal angle) const;
    int turnStart(int page, int turn) const;
    int turnEnd(int page, int turn) const;
    int positionAt(int page, const QPointF &point) const;

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
        qreal theta = 0; // along the spiral, from noon
    };
    struct Glyph
    {
        QString text;   // empty for a paragraph gap
        qreal advance = 0; // angle it takes
        bool gap = false;
    };

    qreal radiusAt(qreal theta) const;
    QPointF pointAt(qreal theta, qreal offset = 0) const;
    qreal maxTheta() const;

    QFont m_font;
    qreal m_diameter = 0;
    qreal m_outerRadius = 0;
    qreal m_innerRadius = 0;
    qreal m_pitch = 0;
    qreal m_baseline = 0;
    int m_pages = 1;
    QList<Stop> m_stops;   // one per cursor position
    QList<Glyph> m_glyphs; // one per character, at the stop of the same index
};
