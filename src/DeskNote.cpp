#include "DeskNote.h"

#include <QApplication>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleHints>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

constexpr int kRadius = 52;
constexpr int kRim = 5;
constexpr int kMargin = 6;

} // namespace

DeskNote::DeskNote(QWidget *parent)
    : QWidget(parent)
{
    // Never in the taskbar. It is kept at the bottom of the stack by hand,
    // see sink(), because changing Qt's window flags recreates the window.
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(2 * (kRadius + kMargin), 2 * (kRadius + kMargin));
    setCursor(Qt::PointingHandCursor);

    QFont font(QStringLiteral("Segoe UI Variable Text"));
    font.setPixelSize(10);
    m_layout.setFont(font);
    m_layout.setDiameter(2 * (kRadius - kRim));
}

void DeskNote::setText(const QString &text)
{
    m_doc.setPlainText(text);
    m_layout.layout(&m_doc);
    setToolTip(text.left(200));
    update();
}

QPoint DeskNote::logicalCenter() const
{
    return geometry().center();
}

QPoint DeskNote::physicalCenter() const
{
    RECT r{};
    GetWindowRect(HWND(winId()), &r);
    return QPoint((r.left + r.right) / 2, (r.top + r.bottom) / 2);
}

void DeskNote::centerOn(const QPoint &logical)
{
    move(logical - QPoint(width() / 2, height() / 2));
}

void DeskNote::sink()
{
    SetWindowPos(HWND(winId()), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(HWND(winId()), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void DeskNote::showEvent(QShowEvent *)
{
    sink();
}

void DeskNote::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    const QPointF c(width() / 2.0, height() / 2.0);

    QRadialGradient shadow(c + QPointF(0, 2), kRadius + kMargin - 1);
    shadow.setColorAt(qreal(kRadius) / (kRadius + kMargin - 1), QColor(0, 0, 0, 60));
    shadow.setColorAt(1, QColor(0, 0, 0, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(shadow);
    p.drawEllipse(c + QPointF(0, 2), kRadius + kMargin - 1, kRadius + kMargin - 1);

    QLinearGradient rim(c.x(), c.y() - kRadius, c.x(), c.y() + kRadius);
    rim.setColorAt(0, QColor(0x7d, 0x5c, 0xe8));
    rim.setColorAt(1, QColor(0x3b, 0x28, 0x91));
    p.setBrush(rim);
    p.drawEllipse(c, kRadius, kRadius);

    const qreal face = kRadius - kRim;
    p.setBrush(dark ? QColor(0x2b, 0x27, 0x33) : QColor(0xff, 0xf7, 0xdf));
    p.drawEllipse(c, face, face);

    // The first page of the writing, small, clipped to the card.
    QPainterPath circle;
    circle.addEllipse(c, face, face);
    p.setClipPath(circle);
    p.translate(c - QPointF(face, face));
    m_layout.draw(p, 0, dark ? QColor(0xec, 0xe6, 0xf5) : QColor(0x2a, 0x24, 0x33), Qt::transparent, 0, 0);
}

void DeskNote::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_pressed = true;
    m_dragging = false;
    m_pressGlobal = event->globalPosition().toPoint();
    m_pressPos = event->position().toPoint();
}

void DeskNote::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_pressed)
        return;
    const QPoint global = event->globalPosition().toPoint();
    if (!m_dragging && (global - m_pressGlobal).manhattanLength() >= QApplication::startDragDistance()) {
        m_dragging = true;
        // Up out of the desktop while it is carried, so it can be seen over
        // the window it is dropped on.
        SetWindowPos(HWND(winId()), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (m_dragging)
        move(global - m_pressPos);
}

void DeskNote::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_pressed)
        return;
    m_pressed = false;
    if (m_dragging) {
        m_dragging = false;
        sink();
        emit dropped();
    } else {
        emit clicked();
    }
}
