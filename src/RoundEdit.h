#pragma once

#include <QBasicTimer>
#include <QElapsedTimer>
#include <QPixmap>
#include <QTextCursor>
#include <QWidget>

class QTextDocument;
class RingTextLayout;

// A plain-text editor whose text runs on concentric rings (see RingTextLayout),
// with a hub at the centre that shows the page and turns it.
//
// It owns a QTextDocument and a QTextCursor, so storage, undo, word movement
// and search are Qt's. Everything to do with geometry is its own: up and down
// move to the neighbouring ring at the same angle, a click is resolved by
// radius and angle, and the view always rests on whole pages.
class RoundEdit : public QWidget
{
    Q_OBJECT

public:
    struct Colors
    {
        QColor ink, selection, guide, hub, hubEdge, hubInk, hubHover;
    };

    explicit RoundEdit(QWidget *parent = nullptr);

    QTextDocument *document() const { return m_doc; }
    RingTextLayout *ringLayout() const { return m_layout; }

    QTextCursor textCursor() const { return m_cursor; }
    void setTextCursor(const QTextCursor &cursor);
    void moveCursor(QTextCursor::MoveOperation operation, QTextCursor::MoveMode mode = QTextCursor::MoveAnchor);

    void setPlainText(const QString &text);
    void clear();
    // The text exactly as typed, lines joined with \n. QTextDocument's own
    // toPlainText() turns non-breaking spaces into ordinary ones.
    QString exactText() const;
    void insertPlainText(const QString &text);

    void undo();
    void redo();
    void cut();
    void copy();
    void paste();
    void selectAll();
    void deleteSelection();

    int currentPage() const { return m_page; }
    int pageCount() const;
    void showPage(int page);

    // Which sheet this is among its place's sheets. The hub shows it, and
    // turning past the first or last page asks for the neighbouring sheet.
    void setSheetMarker(int index, int count);
    bool canTurn(int direction) const;
    void turn(int direction);

    void setColors(const Colors &colors);
    // Pulls the text in from the rim, to leave room for a prompt drawn there.
    void setOuterMargin(qreal margin);
    void setTabStopDistance(qreal distance);

    QPointF center() const;

Q_SIGNALS:
    void cursorPositionChanged();
    void selectionChanged();
    void pageChanged(int page, int count);
    void zoomRequested(int steps);
    void filesDropped(const QStringList &paths);
    void contextMenuRequested(qreal angle);
    // Turned past this sheet's first or last page.
    void sheetTurnRequested(int direction);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void timerEvent(QTimerEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    static bool isEditingKey(const QKeyEvent *event);
    qreal hubRadius() const;
    int hubPart(const QPointF &pos) const; // -1 outside, 0 upper half, 1 lower half
    void relayoutDisc();
    void cursorMoved(bool keepGoal = false);
    void moveByRings(int delta, QTextCursor::MoveMode mode);
    void invalidate();
    void rebuildCache();
    void paintHub(QPainter &painter);
    void paintPreedit(QPainter &painter);

    QTextDocument *m_doc = nullptr;
    RingTextLayout *m_layout = nullptr;
    QTextCursor m_cursor;
    Colors m_colors;

    int m_page = 0;
    int m_sheetIndex = 0;
    int m_sheetCount = 1;
    qreal m_outerMargin = 0;

    QBasicTimer m_blink;
    bool m_cursorOn = true;

    bool m_selecting = false;
    bool m_hasGoal = false;
    qreal m_goalAngle = 0;
    int m_lastPosition = 0;
    int m_lastAnchor = 0;
    QElapsedTimer m_doubleClick;
    int m_hubHover = -1;

    QPixmap m_cache;
    bool m_cacheValid = false;

    // Text an input method is composing and has not committed yet.
    QString m_preedit;

    int m_wheelAccum = 0;
    int m_zoomAccum = 0;
};
