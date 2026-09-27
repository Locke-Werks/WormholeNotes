#pragma once

#include <QColor>
#include <QList>
#include <QString>
#include <QWidget>

class QLineEdit;

// One place a search turned up.
struct SearchHit
{
    QString key;     // the place
    QString label;   // what the place is called
    int sheet = 0;
    int start = 0;   // the match, as positions in the sheet
    int length = 0;
    QString before;  // a little of the writing either side of the match
    QString match;
    QString after;
    QColor colour;   // the sheet's ring colour
};

// Finding a note without remembering where it was written: a round panel
// with a field across the top and the matches below, one row each, naming
// the place and showing the words around the match.
class SearchWindow : public QWidget
{
    Q_OBJECT

public:
    explicit SearchWindow(QWidget *parent = nullptr);

    // Opens centred on a point in logical screen coordinates, field empty.
    void openAt(const QPoint &center);
    void setHits(const QList<SearchHit> &hits);

Q_SIGNALS:
    void queryChanged(const QString &query);
    void chosen(const SearchHit &hit);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QRectF rowRect(int row) const;
    int rowAt(const QPointF &pos) const;
    int visibleRows() const;
    void choose(int index);

    QLineEdit *m_field = nullptr;
    QList<SearchHit> m_hits;
    int m_current = 0;
    int m_hover = -1;
};
