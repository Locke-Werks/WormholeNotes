#pragma once

#include "SpiralLayout.h"

#include <QBasicTimer>
#include <QElapsedTimer>
#include <QTextCursor>
#include <QWidget>

class QTextDocument;

// The paper in the middle of the note: a plain-text editor whose writing runs
// along one spiral groove from the rim to the centre (see SpiralLayout), with
// the page and sheet controls in the middle where the spiral ends.
//
// Storage, undo and word movement are QTextDocument's and QTextCursor's.
// Everything to do with where the text sits is its own.
class NoteFace : public QWidget
{
    Q_OBJECT

public:
    struct Colors
    {
        QColor paper, paperEdge, ink, rule, selection, caret, control, controlHot;
    };

    explicit NoteFace(QWidget *parent = nullptr);

    QTextDocument *document() const { return m_doc; }
    QTextCursor textCursor() const { return m_cursor; }
    QString text() const;
    // Replaces the text without it counting as an edit, with the cursor at
    // the start, or at the end on the last page when fromEnd.
    void load(const QString &text, bool fromEnd);

    // Which sheet this is among its place's sheets, for the controls in the
    // middle. Turning past the first or last page asks for the next sheet.
    void setSheetMarker(int index, int count);
    bool canTurn(int direction) const;
    void turn(int direction);

    void setColors(const Colors &colors);
    void setTextFont(const QFont &font);

    void undo();
    void redo();
    void cut();
    void copy();
    void paste();
    void selectAll();
    void deleteSelection();

Q_SIGNALS:
    void sheetTurnRequested(int direction);
    void zoomRequested(int steps);
    void contextMenuRequested(const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
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
    bool event(QEvent *event) override;

private:
    enum Control { NoControl = -1, Back = 0, On = 1 };

    void relayout();
    void cursorMoved(bool keepGoal = false);
    void showPage(int page);
    void moveTurns(int delta, QTextCursor::MoveMode mode);
    void insert(const QString &text);
    QRectF controlRect(Control control) const;
    Control controlAt(const QPointF &pos) const;

    QTextDocument *m_doc = nullptr;
    QTextCursor m_cursor;
    SpiralLayout m_layout;
    Colors m_colors;
    int m_page = 0;
    int m_sheetIndex = 0;
    int m_sheetCount = 1;

    QBasicTimer m_blink;
    bool m_caretOn = true;
    bool m_selecting = false;
    bool m_hasGoal = false;
    qreal m_goalAngle = 0;
    QElapsedTimer m_doubleClick;
    Control m_hover = NoControl;
    int m_wheel = 0;
    int m_zoomWheel = 0;
};
