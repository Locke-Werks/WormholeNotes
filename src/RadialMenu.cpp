#include "RadialMenu.h"

#include "Arc.h"

#include <QAction>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>

#include <cmath>

namespace {

constexpr qreal kItemPad = 12;     // px either side of a label
constexpr qreal kSeparatorGap = 9; // px of empty ring where a menu has a separator
constexpr qreal kDivider = 2.5;    // px between neighbouring items
constexpr qreal kLevelGap = 5;     // px between one band and the next inward

QFont menuFont(int pixels, bool bold)
{
    QFont font(QStringLiteral("Segoe UI Variable Text"));
    font.setPixelSize(pixels);
    if (bold)
        font.setWeight(QFont::DemiBold);
    return font;
}

} // namespace

RadialMenu::RadialMenu(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    hide();
}

void RadialMenu::setDisc(const QPointF &center, qreal radius)
{
    m_center = center;
    m_radius = radius;
    m_labelPixels = qBound(11, qRound(radius * 0.05), 15);
    const int r = qCeil(radius) + 1;
    setMask(QRegion(QRect(qRound(center.x()) - r, qRound(center.y()) - r, 2 * r, 2 * r), QRegion::Ellipse));
    // Bands are sized for the old circle; a resize just closes them.
    if (isOpen())
        close();
}

qreal RadialMenu::bandThickness() const
{
    const QFontMetricsF label(menuFont(m_labelPixels, false));
    const QFontMetricsF shortcut(menuFont(qRound(m_labelPixels * 0.8), false));
    return label.height() + shortcut.height() + 10;
}

bool RadialMenu::selectable(const Item &item) const
{
    return item.action && item.action->isEnabled();
}

void RadialMenu::open(QMenu *menu, qreal angle, bool selectFirst)
{
    m_levels.clear();
    pushLevel(menu, angle, selectFirst);
    show();
    raise();
    setFocus(Qt::PopupFocusReason);
    update();
}

void RadialMenu::close()
{
    if (m_levels.isEmpty() && isHidden())
        return;
    m_levels.clear();
    hide();
    Q_EMIT closed();
}

void RadialMenu::truncate(int levels)
{
    while (m_levels.size() > levels)
        m_levels.removeLast();
}

void RadialMenu::pushLevel(QMenu *menu, qreal angle, bool selectFirst)
{
    // Menus that fill themselves in lazily, like the font list, do it here.
    Q_EMIT menu->aboutToShow();

    Level level;
    level.menu = menu;
    level.outer = m_levels.isEmpty() ? m_radius - 3 : m_levels.last().inner - kLevelGap;
    level.inner = level.outer - bandThickness();
    const qreal mid = (level.outer + level.inner) / 2;

    // Shrink the type until the whole menu fits once round the ring.
    for (int pixels = m_labelPixels;; --pixels) {
        level.labelFont = menuFont(pixels, false);
        level.shortcutFont = menuFont(qRound(pixels * 0.8), false);
        const QFontMetricsF labelMetrics(level.labelFont);
        const QFontMetricsF shortcutMetrics(level.shortcutFont);

        level.items.clear();
        qreal total = 0;
        bool gapPending = false;
        for (QAction *action : menu->actions()) {
            if (!action->isVisible())
                continue;
            if (action->isSeparator()) {
                gapPending = !level.items.isEmpty();
                continue;
            }
            if (gapPending) {
                total += kSeparatorGap / mid;
                gapPending = false;
            }
            Item item;
            item.action = action;
            item.label = Arc::stripMnemonic(action->text(), &item.mnemonic);
            item.shortcut = action->shortcut().toString(QKeySequence::NativeText);
            const qreal width = qMax(Arc::advance(labelMetrics, item.label),
                                     Arc::advance(shortcutMetrics, item.shortcut));
            item.from = total; // relative for now
            item.sweep = (width + 2 * kItemPad) / mid;
            total += item.sweep;
            level.items.append(item);
        }
        if (total <= Arc::kTau * 0.97 || pixels <= 8) {
            const qreal from = angle - total / 2;
            for (Item &item : level.items)
                item.from += from;
            break;
        }
    }

    if (selectFirst) {
        for (int i = 0; i < level.items.size(); ++i) {
            if (selectable(level.items.at(i))) {
                level.current = i;
                break;
            }
        }
    }
    m_levels.append(level);
}

bool RadialMenu::itemAt(const QPointF &pos, int *levelIndex, int *index) const
{
    const qreal distance = QLineF(m_center, pos).length();
    const qreal angle = Arc::angleOf(m_center, pos);
    for (int l = int(m_levels.size()) - 1; l >= 0; --l) {
        const Level &level = m_levels.at(l);
        if (distance < level.inner || distance > level.outer)
            continue;
        for (int i = 0; i < level.items.size(); ++i) {
            const Item &item = level.items.at(i);
            if (Arc::within(angle, item.from, item.sweep)) {
                *levelIndex = l;
                *index = i;
                return true;
            }
        }
    }
    return false;
}

void RadialMenu::activate(int levelIndex, int index)
{
    const Item item = m_levels.at(levelIndex).items.at(index);
    if (!selectable(item))
        return;
    if (QMenu *submenu = item.action->menu()) {
        truncate(levelIndex + 1);
        m_levels[levelIndex].current = index;
        pushLevel(submenu, item.from + item.sweep / 2, true);
        update();
        return;
    }
    // Close before triggering, so whatever the action opens gets the focus.
    QAction *action = item.action;
    close();
    action->trigger();
}

void RadialMenu::step(int direction)
{
    if (m_levels.isEmpty())
        return;
    Level &level = m_levels.last();
    const int count = int(level.items.size());
    int i = level.current;
    for (int n = 0; n < count; ++n) {
        i = i < 0 ? (direction > 0 ? 0 : count - 1) : (i + direction + count) % count;
        if (selectable(level.items.at(i))) {
            level.current = i;
            break;
        }
    }
    update();
}

bool RadialMenu::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (!(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
            key->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

void RadialMenu::keyPressEvent(QKeyEvent *event)
{
    if (m_levels.isEmpty())
        return;
    switch (event->key()) {
    case Qt::Key_Escape:
    case Qt::Key_Backspace:
        if (m_levels.size() > 1) {
            truncate(m_levels.size() - 1);
            update();
        } else {
            close();
        }
        return;
    case Qt::Key_Right:
    case Qt::Key_Down:
    case Qt::Key_Tab:
        step(1);
        return;
    case Qt::Key_Left:
    case Qt::Key_Up:
    case Qt::Key_Backtab:
        step(-1);
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        if (m_levels.last().current >= 0)
            activate(int(m_levels.size()) - 1, m_levels.last().current);
        return;
    default:
        break;
    }

    const QString text = event->text().toLower();
    if (!text.isEmpty()) {
        const Level &level = m_levels.last();
        for (const bool byMnemonic : { true, false }) {
            for (int i = 0; i < level.items.size(); ++i) {
                const Item &item = level.items.at(i);
                const QChar key = byMnemonic ? item.mnemonic : item.label.left(1).toLower().at(0);
                if (!key.isNull() && key == text.at(0) && selectable(item)) {
                    activate(int(m_levels.size()) - 1, i);
                    return;
                }
            }
        }
    }
}

void RadialMenu::mouseMoveEvent(QMouseEvent *event)
{
    int levelIndex = -1;
    int index = -1;
    if (!itemAt(event->position(), &levelIndex, &index)) {
        setCursor(Qt::ArrowCursor);
        return;
    }
    const Item &item = m_levels.at(levelIndex).items.at(index);
    setCursor(selectable(item) ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (m_levels.at(levelIndex).current == index && m_levels.size() > levelIndex + 1)
        return;

    // Hovering an item closes whatever was open beyond its band, and hovering a
    // submenu opens it: the ring follows the pointer like a cascading menu.
    truncate(levelIndex + 1);
    m_levels[levelIndex].current = selectable(item) ? index : -1;
    if (selectable(item) && item.action->menu())
        pushLevel(item.action->menu(), item.from + item.sweep / 2, false);
    update();
}

void RadialMenu::mousePressEvent(QMouseEvent *event)
{
    int levelIndex = -1;
    int index = -1;
    if (itemAt(event->position(), &levelIndex, &index)) {
        activate(levelIndex, index);
        return;
    }
    close();
}

void RadialMenu::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    p.setPen(Qt::NoPen);
    p.setBrush(m_colors.shade);
    p.drawEllipse(m_center, m_radius, m_radius);

    for (int l = 0; l < m_levels.size(); ++l) {
        const Level &level = m_levels.at(l);
        const qreal mid = (level.outer + level.inner) / 2;
        const QFontMetricsF labelMetrics(level.labelFont);
        const QFontMetricsF shortcutMetrics(level.shortcutFont);
        const qreal lineGap = (level.outer - level.inner - labelMetrics.height() - shortcutMetrics.height()) / 3;
        const qreal outerLine = level.outer - lineGap - labelMetrics.height() / 2;
        const qreal innerLine = level.inner + lineGap + shortcutMetrics.height() / 2;

        for (int i = 0; i < level.items.size(); ++i) {
            const Item &item = level.items.at(i);
            if (!item.action)
                continue;
            const bool enabled = selectable(item);
            const bool current = level.current == i && enabled;
            const qreal inset = (kDivider / 2) / mid;
            const QPainterPath segment = Arc::sector(m_center, level.inner, level.outer, item.from + inset,
                                                     item.sweep - 2 * inset);
            p.setPen(QPen(m_colors.bandEdge, 1));
            p.setBrush(current ? m_colors.accent : m_colors.band);
            p.drawPath(segment);

            const QColor ink = current ? m_colors.accentInk : (enabled ? m_colors.ink : m_colors.dim);
            QColor faint = ink;
            faint.setAlphaF(ink.alphaF() * 0.62);
            const qreal centre = item.from + item.sweep / 2;

            // The label is the first line read. In the lower half the text is
            // turned so its head points at the centre, so there the inner line
            // is the one on top.
            const bool flipped = Arc::flipsAt(centre);
            if (item.shortcut.isEmpty()) {
                Arc::drawCentered(p, m_center, mid, centre, item.label, level.labelFont, ink);
            } else {
                Arc::drawCentered(p, m_center, flipped ? innerLine : outerLine, centre, item.label,
                                  level.labelFont, ink);
                Arc::drawCentered(p, m_center, flipped ? outerLine : innerLine, centre, item.shortcut,
                                  level.shortcutFont, faint);
            }

            if (item.action->isCheckable() && item.action->isChecked()) {
                p.setPen(Qt::NoPen);
                p.setBrush(ink);
                p.drawEllipse(Arc::polar(m_center, level.outer - 4.5, centre), 2.4, 2.4);
            }
            if (item.action->menu()) {
                // A notch on the inner edge: this one opens further in.
                const qreal half = 4.5 / level.inner;
                const QPolygonF notch({ Arc::polar(m_center, level.inner + 2, centre),
                                        Arc::polar(m_center, level.inner + 7, centre - half),
                                        Arc::polar(m_center, level.inner + 7, centre + half) });
                p.setPen(Qt::NoPen);
                p.setBrush(ink);
                p.drawPolygon(notch);
            }
        }
    }
}
