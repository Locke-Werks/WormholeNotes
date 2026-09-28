#pragma once

#include <QColor>
#include <QList>
#include <QString>
#include <QWidget>

// The settings, in a round panel like Find in Notes: one row per setting,
// its name on the left and its value on the right. Clicking a row, or Space
// and the arrow keys, steps its value; the controller decides what the
// values are and hands the rows back.
class SettingsWindow : public QWidget
{
    Q_OBJECT

public:
    struct Row
    {
        QString name;
        QString value;
        QColor colour; // the value's colour, invalid for plain text
        QString note;  // a line under the value, such as why it was skipped
    };

    explicit SettingsWindow(QWidget *parent = nullptr);

    void openAt(const QPoint &center);
    void setRows(const QList<Row> &rows);

Q_SIGNALS:
    // Step the row's value forward (+1) or back (-1).
    void stepped(int row, int direction);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    QRectF rowRect(int row) const;
    int rowAt(const QPointF &pos) const;

    QList<Row> m_rows;
    int m_current = 0;
    int m_hover = -1;
};
