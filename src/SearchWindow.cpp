#include "SearchWindow.h"

#include "Round.h"
#include "Theme.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTimer>

namespace {

constexpr int kRadius = 240;
constexpr int kMargin = 16;
constexpr int kRim = 22;
constexpr int kRows = 7;

} // namespace

SearchWindow::SearchWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setFixedSize(2 * (kRadius + kMargin), 2 * (kRadius + kMargin));

    m_field = new QLineEdit(this);
    m_field->setPlaceholderText(tr("Search every note"));
    m_field->setFont(Theme::bodyFont(16));
    m_field->setFrame(false);
    m_field->setAlignment(Qt::AlignCenter);
    m_field->setStyleSheet(QStringLiteral("QLineEdit { background: %1; color: %2; border: 1px solid %3;"
                                          " border-radius: 4px; padding: 6px 12px;"
                                          " selection-background-color: %4; }")
                               .arg(Theme::input().name(), Theme::textPrimary().name(),
                                    Theme::accentAt(115).name(QColor::HexArgb), Theme::accentAt(60).name(QColor::HexArgb)));
    const int width = int(kRadius * 1.2);
    m_field->setGeometry(kMargin + kRadius - width / 2, kMargin + int(kRadius * 0.36), width, 38);
    m_field->installEventFilter(this);
    connect(m_field, &QLineEdit::textChanged, this, &SearchWindow::queryChanged);
}

void SearchWindow::openAt(const QPoint &center)
{
    const QScreen *s = QGuiApplication::screenAt(center);
    if (!s)
        s = QGuiApplication::primaryScreen();
    const QRect area = s->availableGeometry();
    const int reach = kRadius + kMargin;
    move(qBound(area.left(), center.x() - reach, qMax(area.left(), area.right() - 2 * reach)),
         qBound(area.top(), center.y() - reach, qMax(area.top(), area.bottom() - 2 * reach)));
    m_field->clear();
    m_hits.clear();
    m_current = 0;
    show();
    raise();
    activateWindow();
    m_field->setFocus();
}

void SearchWindow::setHits(const QList<SearchHit> &hits)
{
    m_hits = hits;
    m_current = 0;
    m_hover = -1;
    update();
}

int SearchWindow::visibleRows() const
{
    return int(qMin(qsizetype(kRows), m_hits.size()));
}

QRectF SearchWindow::rowRect(int row) const
{
    // Rows under the field, each as wide as the circle allows at its height.
    const qreal c = kMargin + kRadius;
    const qreal top = m_field->geometry().bottom() + 18;
    const qreal height = 40;
    const qreal y = top + row * height;
    const qreal inner = kRadius - kRim - 10;
    const qreal dy = qMax(std::abs(y - c), std::abs(y + height - c));
    const qreal half = dy < inner ? std::sqrt(inner * inner - dy * dy) : 0;
    return QRectF(c - half, y, 2 * half, height - 4);
}

int SearchWindow::rowAt(const QPointF &pos) const
{
    for (int i = 0; i < visibleRows(); ++i) {
        if (rowRect(i).contains(pos))
            return i;
    }
    return -1;
}

void SearchWindow::paintEvent(QPaintEvent *)
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

    // The same rim as the note: raised, hairline-edged, lit inside.
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

    const QFont label = Theme::labelFont(11);
    Round::arcText(p, c, r - kRim / 2.0, Round::kNoon, tr("Find in notes"), label, Theme::textLabel());

    const QRectF field = m_field->geometry();
    if (m_field->text().trimmed().isEmpty()) {
        p.setFont(Theme::labelFont(9));
        p.setPen(Theme::textFaint());
        p.drawText(QRectF(c.x() - face, field.bottom() + 20, 2 * face, 20), Qt::AlignCenter,
                   tr("Every sheet, every place"));
        return;
    }
    if (m_hits.isEmpty()) {
        p.setFont(Theme::serifFont(18));
        p.setPen(Theme::textSecondary());
        p.drawText(QRectF(c.x() - face, field.bottom() + 24, 2 * face, 30), Qt::AlignCenter, tr("Nothing written like that"));
        return;
    }

    const QFont body = Theme::bodyFont(13);
    const QFontMetricsF metrics(body);
    for (int i = 0; i < visibleRows(); ++i) {
        const SearchHit &hit = m_hits.at(i);
        const QRectF row = rowRect(i);
        if (row.width() < 40)
            continue;
        const bool current = i == m_current;
        if (current || i == m_hover) {
            p.setPen(current ? QPen(Theme::withAlpha(hit.colour, 115), 1) : Qt::NoPen);
            p.setBrush(Theme::withAlpha(hit.colour, current ? 34 : 18));
            p.drawRoundedRect(row, 4, 4);
        }
        // The place's name, a small lit dot in its sheet's colour, then the
        // words around the match with the match itself bright.
        p.setPen(Qt::NoPen);
        p.setBrush(hit.colour);
        p.drawEllipse(QPointF(row.left() + 10, row.center().y()), 3, 3);
        p.setFont(Theme::labelFont(9));
        p.setPen(Theme::textLabel());
        const QRectF name(row.left() + 20, row.top() + 2, row.width() - 26, 14);
        p.drawText(name, Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetricsF(p.font()).elidedText(hit.label, Qt::ElideRight, name.width()));

        p.setFont(body);
        const qreal width = row.width() - 26;
        const qreal matchWidth = metrics.horizontalAdvance(hit.match);
        const qreal side = qMax(0.0, (width - matchWidth) / 2);
        const QString before = metrics.elidedText(hit.before, Qt::ElideLeft, side);
        const QString after = metrics.elidedText(hit.after, Qt::ElideRight, side);
        qreal x = row.left() + 20 + side - metrics.horizontalAdvance(before);
        const qreal baseline = row.bottom() - 7;
        p.setPen(Theme::textSecondary());
        p.drawText(QPointF(x, baseline), before);
        x += metrics.horizontalAdvance(before);
        p.setPen(Theme::textPrimary());
        p.drawText(QPointF(x, baseline), hit.match);
        p.setPen(Theme::withAlpha(hit.colour, 200));
        p.drawLine(QPointF(x, baseline + 3), QPointF(x + matchWidth, baseline + 3));
        x += matchWidth;
        p.setPen(Theme::textSecondary());
        p.drawText(QPointF(x, baseline), after);
    }
    if (m_hits.size() > kRows) {
        p.setFont(Theme::labelFont(9));
        p.setPen(Theme::textFaint());
        p.drawText(QRectF(c.x() - face, rowRect(kRows - 1).bottom() + 4, 2 * face, 16), Qt::AlignCenter,
                   tr("%1 more").arg(m_hits.size() - kRows));
    }
}

void SearchWindow::mouseMoveEvent(QMouseEvent *event)
{
    const int row = rowAt(event->position());
    if (row != m_hover) {
        m_hover = row;
        setCursor(row >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void SearchWindow::mousePressEvent(QMouseEvent *event)
{
    const int row = rowAt(event->position());
    if (row >= 0) {
        choose(row);
        return;
    }
    // Outside the circle is outside the panel.
    const QPointF c(kMargin + kRadius, kMargin + kRadius);
    if (QLineF(c, event->position()).length() > kRadius)
        hide();
}

void SearchWindow::choose(int index)
{
    if (index < 0 || index >= m_hits.size())
        return;
    const SearchHit hit = m_hits.at(index);
    hide();
    emit chosen(hit);
}

void SearchWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        hide();
    else
        QWidget::keyPressEvent(event);
}

// The field keeps the focus; the keys that move through the results are
// taken from it on the way.
bool SearchWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_field && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        switch (key->key()) {
        case Qt::Key_Down:
            m_current = qMin(m_current + 1, visibleRows() - 1);
            update();
            return true;
        case Qt::Key_Up:
            m_current = qMax(m_current - 1, 0);
            update();
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            choose(m_current);
            return true;
        case Qt::Key_Escape:
            hide();
            return true;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SearchWindow::changeEvent(QEvent *event)
{
    // Clicking away closes it, like the note.
    if (event->type() == QEvent::ActivationChange && !isActiveWindow()) {
        QTimer::singleShot(0, this, [this] {
            if (!QApplication::activeWindow() || QApplication::activeWindow() != this)
                hide();
        });
    }
    QWidget::changeEvent(event);
}
