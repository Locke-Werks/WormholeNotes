#pragma once

#include <QColor>
#include <QWidget>

// Where a note is dropped to tear it off. It appears at the foot of the
// screen while a note is being dragged by its rim, so tearing off never
// depends on there being any bare desktop to aim at; most of the time there
// is none. It never takes the mouse, so the drag carries on underneath it.
class TearTarget : public QWidget
{
    Q_OBJECT

public:
    explicit TearTarget(QWidget *parent = nullptr);

    // Shows it at the foot of the screen that holds a point, in the sheet's
    // colour.
    void appear(const QPoint &onScreen, const QColor &colour);
    // Lights it while the pointer is over it; true when it is.
    bool track();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QColor m_colour;
    bool m_hot = false;
};
