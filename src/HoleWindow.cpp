#include "HoleWindow.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

constexpr int kRadius = 13;
constexpr int kMargin = 4; // room for the hover glow

} // namespace

HoleWindow::HoleWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus
                   | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFixedSize(2 * (kRadius + kMargin), 2 * (kRadius + kMargin));
    setCursor(Qt::PointingHandCursor);
}

void HoleWindow::setFilled(bool filled)
{
    if (filled == m_filled)
        return;
    m_filled = filled;
    update();
}

void HoleWindow::placeAt(const QPoint &c)
{
    HWND hwnd = HWND(winId());
    RECT r{};
    GetWindowRect(hwnd, &r);
    const int w = r.right - r.left;
    const int h = r.bottom - r.top;
    SetWindowPos(hwnd, HWND_TOPMOST, c.x() - w / 2, c.y() - h / 2, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    if (!isVisible())
        show();
}

QPoint HoleWindow::physicalCenter() const
{
    RECT r{};
    GetWindowRect(HWND(winId()), &r);
    return QPoint((r.left + r.right) / 2, (r.top + r.bottom) / 2);
}

void HoleWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c(width() / 2.0, height() / 2.0);
    const qreal r = kRadius;

    if (m_hover) {
        QRadialGradient glow(c, r + kMargin);
        glow.setColorAt(0.7, QColor(0x2e, 0xe8, 0xff, 90));
        glow.setColorAt(1, QColor(0x2e, 0xe8, 0xff, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(c, r + kMargin, r + kMargin);
    }

    // The rim: Tondo's bezel, shrunk to a grommet.
    QLinearGradient rim(c.x(), c.y() - r, c.x(), c.y() + r);
    rim.setColorAt(0, QColor(0xe2, 0x36, 0x3b));
    rim.setColorAt(1, QColor(0xa4, 0x1b, 0x20));
    p.setPen(Qt::NoPen);
    p.setBrush(rim);
    p.drawEllipse(c, r, r);

    // The tunnel. A hole with a note behind it glows; an empty one is dark.
    const qreal inner = r - 3.5;
    QRadialGradient tunnel(c, inner);
    if (m_filled) {
        tunnel.setColorAt(0, QColor(0xe8, 0xfd, 0xff));
        tunnel.setColorAt(0.35, QColor(0x2e, 0xe8, 0xff));
        tunnel.setColorAt(1, QColor(0xb0, 0x5c, 0xf6));
    } else {
        tunnel.setColorAt(0, QColor(0x2e, 0x4a, 0x66));
        tunnel.setColorAt(1, QColor(0x14, 0x12, 0x1e));
    }
    p.setBrush(tunnel);
    p.drawEllipse(c, inner, inner);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 110), 1.2));
    p.drawEllipse(c, inner, inner);
}

void HoleWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_pressed = true;
    m_dragging = false;
    m_pressGlobal = event->globalPosition().toPoint();
    m_pressPos = event->position().toPoint();
}

void HoleWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_pressed)
        return;
    const QPoint global = event->globalPosition().toPoint();
    if (!m_dragging && (global - m_pressGlobal).manhattanLength() >= QApplication::startDragDistance())
        m_dragging = true;
    if (m_dragging)
        move(global - m_pressPos);
}

void HoleWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_pressed)
        return;
    m_pressed = false;
    if (m_dragging) {
        m_dragging = false;
        emit dragFinished(physicalCenter());
    } else {
        emit clicked();
    }
}

void HoleWindow::enterEvent(QEnterEvent *)
{
    m_hover = true;
    update();
}

void HoleWindow::leaveEvent(QEvent *)
{
    m_hover = false;
    update();
}
