#pragma once

#include <QPoint>
#include <QWidget>

// The punched hole that rides on the active window: a small topmost disc
// that never takes focus. Click it and the note opens out of it; drag it and
// it moves to another spot on the window.
class HoleWindow : public QWidget
{
    Q_OBJECT

public:
    explicit HoleWindow(QWidget *parent = nullptr);

    // A hole with writing behind it looks deeper than an empty one.
    void setFilled(bool filled);
    bool isDragging() const { return m_dragging; }

    // Centre the hole on a point in physical screen pixels, keeping it on top
    // without activating it. Physical, because that is what the tracked
    // window's rectangle is in, and Qt's logical coordinates differ per screen.
    void placeAt(const QPoint &physicalCenter);
    QPoint physicalCenter() const;

Q_SIGNALS:
    void clicked();
    void dragFinished(const QPoint &physicalCenter);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    bool m_filled = false;
    bool m_hover = false;
    bool m_pressed = false;
    bool m_dragging = false;
    QPoint m_pressGlobal;
    QPoint m_pressPos;
};
