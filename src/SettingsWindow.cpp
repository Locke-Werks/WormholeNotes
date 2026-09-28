#include "SettingsWindow.h"

#include "Round.h"
#include "Theme.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTimer>

namespace {

constexpr int kRadius = 200;
constexpr int kMargin = 16;
constexpr int kRim = 22;
constexpr qreal kRowHeight = 52;

} // namespace

SettingsWindow::SettingsWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setFixedSize(2 * (kRadius + kMargin), 2 * (kRadius + kMargin));
}

void SettingsWindow::openAt(const QPoint &center)
{
    const QScreen *s = QGuiApplication::screenAt(center);
    if (!s)
        s = QGuiApplication::primaryScreen();
    const QRect area = s->availableGeometry();
    const int reach = kRadius + kMargin;
    move(qBound(area.left(), center.x() - reach, qMax(area.left(), area.right() - 2 * reach)),
         qBound(area.top(), center.y() - reach, qMax(area.top(), area.bottom() - 2 * reach)));
    m_current = 0;
    show();
    raise();
    activateWindow();
    setFocus();
}

void SettingsWindow::setRows(const QList<Row> &rows)
{
    m_rows = rows;
    m_current = qBound(0, m_current, int(m_rows.size()) - 1);
    update();
}

QRectF SettingsWindow::rowRect(int row) const
{
    // The rows sit centred on the face, each as wide as the circle allows at
    // its height.
    const qreal c = kMargin + kRadius;
    const qreal top = c - kRowHeight * m_rows.size() / 2.0 + 10;
    const qreal y = top + row * kRowHeight;
    const qreal inner = kRadius - kRim - 12;
    const qreal dy = qMax(std::abs(y - c), std::abs(y + kRowHeight - c));
    const qreal half = dy < inner ? std::sqrt(inner * inner - dy * dy) : 0;
    return QRectF(c - half, y, 2 * half, kRowHeight - 6);
}

int SettingsWindow::rowAt(const QPointF &pos) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (rowRect(i).contains(pos))
            return i;
    }
    return -1;
}

void SettingsWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c(kMargin + kRadius, kMargin + kRadius);
    const qreal r = kRadius;

    QRadialGradient shadow(c + QPointF(0, 4), r + kMargin - 2);
    shadow.setColorAt(r / (r + kMargin - 2), QColor(0, 0, 0, 110));
    shadow.setColorAt(1, QColor(0, 0, 0, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(shadow);
    p.drawEllipse(c + QPointF(0, 4), r + kMargin - 2, r + kMargin - 2);

    // The same rim as the note and Find in Notes.
    p.setPen(QPen(Theme::hairlineStrong(), 1));
    p.setBrush(Theme::raised());
    p.drawEllipse(c, r - 0.5, r - 0.5);
    const qreal face = r - kRim;
    p.setPen(QPen(Theme::accentAt(200), 1.4));
    p.setBrush(Theme::ground());
    p.drawEllipse(c, face, face);

    QPainterPath circle;
    circle.addEllipse(c, face, face);
    p.setClipPath(circle);
    QRadialGradient bloom(c.x(), c.y() - face * 1.2, face * 1.3);
    bloom.setColorAt(0, Theme::accentAt(24));
    bloom.setColorAt(1, Theme::accentAt(0));
    p.fillRect(QRectF(c.x() - face, c.y() - face, 2 * face, 2 * face), bloom);
    p.setClipping(false);

    Round::arcText(p, c, r - kRim / 2.0, Round::kNoon, tr("Settings"), Theme::labelFont(11), Theme::textLabel());

    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &row = m_rows.at(i);
        const QRectF box = rowRect(i);
        if (box.width() < 40)
            continue;
        const QColor tint = row.colour.isValid() ? row.colour : Theme::accent();
        const bool current = i == m_current && hasFocus();
        if (current || i == m_hover) {
            p.setPen(current ? QPen(Theme::withAlpha(tint, 115), 1) : Qt::NoPen);
            p.setBrush(Theme::withAlpha(tint, current ? 34 : 18));
            p.drawRoundedRect(box, 4, 4);
        }
        const QRectF inside = box.adjusted(14, 0, -14, 0);
        p.setFont(Theme::labelFont(10));
        p.setPen(Theme::textLabel());
        p.drawText(inside, Qt::AlignLeft | Qt::AlignVCenter, row.name.toUpper());

        // The value, with the little arrows that say it steps.
        p.setFont(Theme::bodyFont(15));
        const QFontMetricsF metrics(p.font());
        const QString value = QStringLiteral("‹  %1  ›").arg(row.value);
        const qreal width = metrics.horizontalAdvance(value);
        QRectF valueBox(inside.right() - width, inside.top(), width, inside.height());
        if (!row.note.isEmpty())
            valueBox.translate(0, -7);
        p.setPen(row.colour.isValid() ? row.colour : Theme::textPrimary());
        p.drawText(valueBox, Qt::AlignRight | Qt::AlignVCenter, value);
        if (!row.note.isEmpty()) {
            p.setFont(Theme::labelFont(8));
            p.setPen(Theme::textFaint());
            p.drawText(QRectF(inside.left(), inside.center().y() + 4, inside.width(), 16), Qt::AlignRight | Qt::AlignVCenter,
                       row.note);
        }
    }

    p.setFont(Theme::labelFont(8));
    p.setPen(Theme::textFaint());
    const qreal below = m_rows.isEmpty() ? c.y() : rowRect(int(m_rows.size()) - 1).bottom() + 10;
    p.drawText(QRectF(c.x() - face, below, 2 * face, 16), Qt::AlignCenter, tr("Click a row to change it"));
}

void SettingsWindow::mouseMoveEvent(QMouseEvent *event)
{
    const int row = rowAt(event->position());
    if (row != m_hover) {
        m_hover = row;
        setCursor(row >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void SettingsWindow::mousePressEvent(QMouseEvent *event)
{
    const int row = rowAt(event->position());
    if (row >= 0) {
        m_current = row;
        emit stepped(row, event->button() == Qt::RightButton ? -1 : 1);
        return;
    }
    const QPointF c(kMargin + kRadius, kMargin + kRadius);
    if (QLineF(c, event->position()).length() > kRadius)
        hide();
}

void SettingsWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        hide();
        return;
    case Qt::Key_Up:
        m_current = qMax(m_current - 1, 0);
        break;
    case Qt::Key_Down:
        m_current = qMin(m_current + 1, int(m_rows.size()) - 1);
        break;
    case Qt::Key_Left:
        emit stepped(m_current, -1);
        break;
    case Qt::Key_Right:
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        emit stepped(m_current, 1);
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    update();
}

void SettingsWindow::changeEvent(QEvent *event)
{
    // Clicking away closes it, like Find in Notes.
    if (event->type() == QEvent::ActivationChange && !isActiveWindow()) {
        QTimer::singleShot(0, this, [this] {
            if (QApplication::activeWindow() != this)
                hide();
        });
    }
    QWidget::changeEvent(event);
}
