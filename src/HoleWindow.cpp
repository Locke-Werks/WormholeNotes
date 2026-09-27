#include "HoleWindow.h"

#include "Theme.h"

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

void HoleWindow::setColour(const QColor &colour)
{
    if (colour == m_colour)
        return;
    m_colour = colour;
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
    const QColor colour = m_colour.isValid() ? m_colour : Theme::accent();

    // Light spilling out of the hole: only when something is written behind
    // it, stronger under the pointer.
    if (m_filled || m_hover) {
        QRadialGradient glow(c, r + kMargin);
        glow.setColorAt(0.55, Theme::withAlpha(colour, m_hover ? 140 : 90));
        glow.setColorAt(1, Theme::withAlpha(colour, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(c, r + kMargin, r + kMargin);
    }

    // The grommet: a raised ring with a hairline edge.
    p.setPen(QPen(Theme::hairlineStrong(), 1));
    p.setBrush(Theme::raised());
    p.drawEllipse(c, r, r);

    // The tunnel: the ground, and a lit core when there is writing behind it.
    const qreal inner = r - 3.5;
    QRadialGradient tunnel(c, inner);
    if (m_filled) {
        tunnel.setColorAt(0, colour.lighter(140));
        tunnel.setColorAt(0.45, colour);
        tunnel.setColorAt(1, Theme::ground());
    } else {
        tunnel.setColorAt(0, Theme::pressed());
        tunnel.setColorAt(1, Theme::ground());
    }
    p.setPen(QPen(m_filled ? Theme::withAlpha(colour, 200) : Theme::hairline(), 1));
    p.setBrush(tunnel);
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
    if (!m_dragging && (global - m_pressGlobal).manhattanLength() >= QApplication::startDragDistance()) {
        m_dragging = true;
        emit dragStarted();
    }
    if (m_dragging) {
        move(global - m_pressPos);
        emit dragging();
    }
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
