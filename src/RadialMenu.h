#pragma once

#include <QColor>
#include <QList>
#include <QWidget>

class QAction;
class QMenu;

// Shows a QMenu as a ring. Each item is a segment of a band just inside the
// bezel, its label running along the arc; the band is centred on the angle it
// was opened from. A submenu opens as a second band inside the first, centred
// on its parent item, and so on inward.
//
// The QMenu is only the model. It is never shown: its actions supply the text,
// shortcut, enabled and checked state, and are triggered when chosen.
class RadialMenu : public QWidget
{
    Q_OBJECT

public:
    struct Colors
    {
        QColor band, bandEdge, ink, dim, accent, accentInk, shade;
    };

    explicit RadialMenu(QWidget *parent);

    // The circle the bands hang inside, in this widget's coordinates.
    void setDisc(const QPointF &center, qreal radius);
    void setColors(const Colors &colors) { m_colors = colors; update(); }

    // `selectFirst` highlights the first item, for a menu opened from the keyboard.
    void open(QMenu *menu, qreal angle, bool selectFirst = false);
    void close();
    bool isOpen() const { return !m_levels.isEmpty(); }
    QMenu *rootMenu() const { return m_levels.isEmpty() ? nullptr : m_levels.first().menu; }

Q_SIGNALS:
    void closed();

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct Item
    {
        QAction *action = nullptr;
        QString label;
        QString shortcut;
        QChar mnemonic;
        qreal from = 0;
        qreal sweep = 0;
    };
    struct Level
    {
        QMenu *menu = nullptr;
        qreal outer = 0;
        qreal inner = 0;
        QFont labelFont;
        QFont shortcutFont;
        QList<Item> items;
        int current = -1;
    };

    void pushLevel(QMenu *menu, qreal angle, bool selectFirst);
    void truncate(int levels);
    bool itemAt(const QPointF &pos, int *level, int *index) const;
    void activate(int level, int index);
    void step(int direction);
    bool selectable(const Item &item) const;
    qreal bandThickness() const;

    QPointF m_center;
    qreal m_radius = 0;
    Colors m_colors;
    QList<Level> m_levels;
    int m_labelPixels = 13;
};
