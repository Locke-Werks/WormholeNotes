#pragma once

#include <QBasicTimer>
#include <QColor>
#include <QList>
#include <QStringList>
#include <QWidget>

class QEventLoop;

// A dialog made of rings. Messages and text fields sit on arcs across the top
// of the face, reading clockwise; toggles and buttons sit on arcs across the
// bottom, reading upright. Every control is a segment of a band just inside
// the bezel, and every piece of text follows its arc.
//
// Modal prompts shade the face and block it; exec() waits for an answer the
// way QDialog::exec() does. Modeless prompts (Find, Replace) cover only their
// own bands, so the text stays live underneath.
class RadialPrompt : public QWidget
{
    Q_OBJECT

public:
    struct Colors
    {
        QColor band, bandEdge, ink, dim, accent, accentInk, shade, field, selection;
    };

    explicit RadialPrompt(QWidget *parent);

    void setDisc(const QPointF &center, qreal radius);
    void setColors(const Colors &colors) { m_colors = colors; update(); }

    void reset(bool modal);
    void addMessage(const QString &text);
    int addField(const QString &label, const QString &text, bool digitsOnly = false);
    int addToggle(const QString &label, bool on);
    void addButton(const QString &label, int id, bool isDefault = false);
    void setCancelId(int id) { m_cancelId = id; }

    void present();
    int exec();
    void dismiss();
    bool isOpen() const { return isVisible(); }
    bool isModal() const { return m_modal; }

    QString fieldText(int field) const;
    bool toggleState(int toggle) const;
    // How far in from the rim the prompt reaches, so the text can make room.
    qreal depth() const;

Q_SIGNALS:
    void buttonClicked(int id);
    void dismissed();

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void timerEvent(QTimerEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    enum class Kind { Field, Toggle, Button };
    struct Control
    {
        Kind kind = Kind::Button;
        QString label;
        QString text;
        bool on = false;
        bool isDefault = false;
        bool digitsOnly = false;
        int id = -1;
        int cursor = 0;
        int anchor = 0;
        qreal scroll = 0;
        // Geometry, filled in by layoutControls().
        int row = 0;
        bool top = true;
        qreal from = 0;      // the control itself
        qreal sweep = 0;
        qreal labelFrom = 0; // a field's label, which precedes it
        qreal labelSweep = 0;
    };

    QFont font(bool bold = false) const;
    qreal bandThickness() const;
    qreal rowOuter(int row) const;
    qreal rowInner(int row) const { return rowOuter(row) - bandThickness(); }
    qreal rowMid(int row) const { return rowOuter(row) - bandThickness() / 2; }
    void layoutControls();
    int controlAt(const QPointF &pos) const;
    void click(int id);
    void focusControl(int index);
    void moveFocus(int direction);
    int defaultButton() const;

    // Field editing.
    Control *focusedField();
    void insertIntoField(Control &field, const QString &text);
    void ensureCursorVisible(Control &field);
    qreal textX(const Control &field, int index) const;
    int indexAtX(const Control &field, qreal x) const;
    qreal fieldTextWidth(const Control &field) const;

    QPointF m_center;
    qreal m_radius = 0;
    Colors m_colors;
    bool m_modal = true;
    QStringList m_messages;
    QList<Control> m_controls;
    int m_focus = -1;
    int m_hover = -1;
    int m_cancelId = -1;
    QEventLoop *m_loop = nullptr;
    int m_result = -1;
    QBasicTimer m_blink;
    bool m_cursorOn = true;
};
