#include "TearTarget.h"

#include "Theme.h"

#include <QCursor>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>

namespace {

constexpr int kRadius = 58;
constexpr int kMargin = 14;

} // namespace

TearTarget::TearTarget(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus
                   | Qt::WindowTransparentForInput | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedSize(2 * (kRadius + kMargin), 2 * (kRadius + kMargin));
}

void TearTarget::appear(const QPoint &onScreen, const QColor &colour)
{
    const QScreen *s = QGuiApplication::screenAt(onScreen);
    if (!s)
        s = QGuiApplication::primaryScreen();
    const QRect area = s->availableGeometry();
    m_colour = colour;
    m_hot = false;
    move(area.center().x() - width() / 2, area.bottom() - height() - 24);
    show();
    raise();
}

bool TearTarget::track()
{
    const QPointF c = QRectF(geometry()).center();
    const bool hot = isVisible() && QLineF(c, QCursor::pos()).length() <= kRadius + 10;
    if (hot != m_hot) {
        m_hot = hot;
        update();
    }
    return hot;
}

void TearTarget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c(width() / 2.0, height() / 2.0);

    if (m_hot) {
        QRadialGradient glow(c, kRadius + kMargin);
        glow.setColorAt(0.7, Theme::withAlpha(m_colour, 110));
        glow.setColorAt(1, Theme::withAlpha(m_colour, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(c, kRadius + kMargin, kRadius + kMargin);
    }

    // An open hole in the house ground, its rim dashed like a tear line.
    p.setBrush(Theme::withAlpha(Theme::ground(), 235));
    QPen rim(m_hot ? m_colour : Theme::withAlpha(m_colour, 170), 2, Qt::DashLine, Qt::RoundCap);
    rim.setDashPattern({ 3, 3 });
    p.setPen(rim);
    p.drawEllipse(c, kRadius, kRadius);

    p.setPen(m_hot ? Theme::textPrimary() : Theme::textLabel());
    p.setFont(Theme::labelFont(10));
    p.drawText(QRectF(c.x() - kRadius, c.y() - 16, 2 * kRadius, 14), Qt::AlignCenter, tr("Tear off"));
    p.setFont(Theme::labelFont(8));
    p.setPen(Theme::textFaint());
    p.drawText(QRectF(c.x() - kRadius, c.y() + 2, 2 * kRadius, 14), Qt::AlignCenter, tr("to the desktop"));
}
