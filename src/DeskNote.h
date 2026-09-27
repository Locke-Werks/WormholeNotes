#pragma once

#include "SpiralLayout.h"

#include <QPoint>
#include <QTextDocument>
#include <QWidget>

// A note torn off onto the desktop: a small round card that sits under the
// windows, like something pinned to the wallpaper, showing the start of what
// is written on it. Click it to open it in the full note; drag it onto a
// window to hand its writing to that place.
class DeskNote : public QWidget
{
    Q_OBJECT

public:
    explicit DeskNote(QWidget *parent = nullptr);

    void setText(const QString &text);
    // Its centre, in logical screen coordinates and in physical pixels.
    QPoint logicalCenter() const;
    QPoint physicalCenter() const;
    void centerOn(const QPoint &logical);
    // Back down to the bottom of the stack, with the desktop.
    void sink();

Q_SIGNALS:
    void clicked();
    void dropped();

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QTextDocument m_doc;
    SpiralLayout m_layout;
    bool m_pressed = false;
    bool m_dragging = false;
    QPoint m_pressGlobal;
    QPoint m_pressPos;
};
