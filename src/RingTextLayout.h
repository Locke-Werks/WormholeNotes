#pragma once

#include <QAbstractTextDocumentLayout>
#include <QList>
#include <QTextBlock>

class QTextLine;

// Lays a plain-text document out on concentric rings.
//
// Every line of text is one ring. A line starts just clockwise of noon, runs
// all the way round, and stops short of noon again, leaving a small gap that
// marks where lines begin. Its length is the ring's circumference less that
// gap, so a line near the rim holds far more than one near the hub. Lines step
// inward ring by ring; when the next ring would cut into the hub, the text
// continues on the outermost ring of the next page.
//
// QTextLayout still does all the shaping and line breaking: each line is just
// given its ring's arc length as a width. Painting then takes each glyph's
// position along that straight line and wraps it onto the ring.
//
// A line's QTextLine y is its global ring index times the pitch, so a ring is
// found from a line and a line from a ring without any side table. Layout is
// incremental: an edit relays out from the changed block and stops at the first
// later block whose first ring did not move.
class RingTextLayout : public QAbstractTextDocumentLayout
{
    Q_OBJECT

public:
    struct Ring
    {
        qreal baseline = 0; // radius of the baseline
        qreal mid = 0;      // radius at which advances are measured
        qreal start = 0;    // angle at which the line begins
        qreal sweep = 0;    // angle the line may use
        qreal width = 0;    // sweep * mid: the QTextLine width
    };

    struct LineRef
    {
        QTextBlock block;
        int index = -1;
        bool isValid() const { return index >= 0; }
    };

    explicit RingTextLayout(QTextDocument *document);

    // The disc the text lives in: the radius of its outermost edge and of the
    // hub it must stay clear of.
    void setDisc(qreal outerRadius, qreal hubRadius);
    qreal outerRadius() const { return m_outer; }
    qreal hubRadius() const { return m_hub; }

    int ringsPerPage() const { return m_ringsPerPage; }
    int ringCount() const { return m_rings; }
    int pageOfRing(int ring) const { return ring / m_ringsPerPage; }
    const Ring &ring(int ring) const { return m_geometry.at(ring % m_ringsPerPage); }
    qreal ascent() const { return m_ascent; }
    qreal descent() const { return m_descent; }

    LineRef lineAtRing(int ring) const;
    int ringOfPosition(int position, qreal *x = nullptr) const;
    int pageOfPosition(int position) const { return pageOfRing(ringOfPosition(position)); }
    qreal angleOfPosition(int position) const;
    int positionOnRing(int ring, qreal angle) const;
    // `point` is relative to the centre of the disc.
    int positionAt(int page, const QPointF &point) const;

    // Paint with the painter's origin at the centre of the disc.
    void paintGuides(QPainter *painter, const QColor &color) const;
    void paintPage(QPainter *painter, int page, const QColor &ink, const QColor &selection,
                   int selectionStart, int selectionEnd) const;
    void paintCursor(QPainter *painter, int page, int position, const QColor &color, qreal width) const;

    void draw(QPainter *painter, const PaintContext &context) override;
    int hitTest(const QPointF &point, Qt::HitTestAccuracy accuracy) const override;
    int pageCount() const override { return m_pages; }
    QSizeF documentSize() const override;
    QRectF frameBoundingRect(QTextFrame *frame) const override;
    QRectF blockBoundingRect(const QTextBlock &block) const override;

protected:
    void documentChanged(int from, int charsRemoved, int charsAdded) override;

private:
    void computeGeometry();
    void relayout(int from, int changeEnd);
    int layoutBlock(const QTextBlock &block, int firstRing);
    int firstRing(const QTextBlock &block) const;
    int endRing(const QTextBlock &block) const;
    QPointF glyphPoint(const Ring &ring, qreal radius, qreal x) const;

    qreal m_outer = 0;
    qreal m_hub = 0;
    qreal m_ascent = 0;
    qreal m_descent = 0;
    qreal m_pitch = 1;
    QString m_fontKey;
    QList<Ring> m_geometry;
    int m_ringsPerPage = 1;
    int m_rings = 1;
    int m_pages = 1;
};
