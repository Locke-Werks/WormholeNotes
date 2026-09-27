#include "DeskNote.h"

#include "Theme.h"

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

    m_layout.setFont(Theme::bodyFont(10));
    m_colour = Theme::accent();
    m_layout.setDiameter(2 * (kRadius - kRim));
}

void DeskNote::setText(const QString &text)
{
    m_doc.setPlainText(text);
    m_layout.layout(&m_doc);
    setToolTip(text.left(200));
    update();
}

void DeskNote::setColour(const QColor &colour)
{
    m_colour = colour;
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
    if (m_raised) {
        SetWindowPos(HWND(winId()), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return;
    }
    SetWindowPos(HWND(winId()), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(HWND(winId()), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void DeskNote::setRaised(bool raised)
{
    m_raised = raised;
    if (isVisible())
        sink();
}

void DeskNote::showEvent(QShowEvent *)
{
    sink();
}

void DeskNote::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c(width() / 2.0, height() / 2.0);

    QRadialGradient shadow(c + QPointF(0, 2), kRadius + kMargin - 1);
    shadow.setColorAt(qreal(kRadius) / (kRadius + kMargin - 1), QColor(0, 0, 0, 90));
    shadow.setColorAt(1, QColor(0, 0, 0, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(shadow);
    p.drawEllipse(c + QPointF(0, 2), kRadius + kMargin - 1, kRadius + kMargin - 1);

    // The same rim as the note, small: raised, hairline-edged, lit inside in
    // the sheet's colour.
    p.setPen(QPen(Theme::hairlineStrong(), 1));
    p.setBrush(Theme::raised());
    p.drawEllipse(c, kRadius, kRadius);
    const qreal face = kRadius - kRim;
    p.setPen(QPen(Theme::withAlpha(m_colour, 190), 1.2));
    p.setBrush(Theme::ground());
    p.drawEllipse(c, face, face);

    // The first page of the writing, small, clipped to the card.
    QPainterPath circle;
    circle.addEllipse(c, face, face);
    p.setClipPath(circle);
    QRadialGradient bloom(c.x(), c.y() - face * 1.2, face * 1.4);
    bloom.setColorAt(0, Theme::withAlpha(m_colour, 30));
    bloom.setColorAt(1, Theme::withAlpha(m_colour, 0));
    p.fillRect(QRectF(c.x() - face, c.y() - face, 2 * face, 2 * face), bloom);
    p.translate(c - QPointF(face, face));
    // The newest page, the words last written at the top.
    m_layout.draw(p, m_layout.pageCount() - 1, Theme::textBody(), Qt::transparent, m_colour, 0, 0);
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
