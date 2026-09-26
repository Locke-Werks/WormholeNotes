#include "RadialPrompt.h"

#include "Arc.h"

#include <QApplication>
#include <QClipboard>
#include <QEventLoop>
#include <QFontMetricsF>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QTextBoundaryFinder>

#include <cmath>

namespace {

constexpr qreal kPad = 12;       // px of band either side of a label
constexpr qreal kGap = 4;        // px between neighbouring controls
constexpr qreal kRowGap = 5;     // px between one band and the next inward
constexpr qreal kFieldSweep = 1.95; // radians, about 112 degrees

qsizetype nextBoundary(const QString &text, qsizetype from)
{
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    finder.setPosition(from);
    const qsizetype next = finder.toNextBoundary();
    return next < 0 ? text.size() : next;
}

qsizetype previousBoundary(const QString &text, qsizetype from)
{
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    finder.setPosition(from);
    const qsizetype previous = finder.toPreviousBoundary();
    return previous < 0 ? 0 : previous;
}

} // namespace

RadialPrompt::RadialPrompt(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled);
    setMouseTracking(true);
    hide();
}

QFont RadialPrompt::font(bool bold) const
{
    QFont f(QStringLiteral("Segoe UI Variable Text"));
    f.setPixelSize(qBound(12, qRound(m_radius * 0.052), 16));
    if (bold)
        f.setWeight(QFont::DemiBold);
    return f;
}

qreal RadialPrompt::bandThickness() const
{
    return QFontMetricsF(font()).height() + 16;
}

qreal RadialPrompt::rowOuter(int row) const
{
    return m_radius - 4 - row * (bandThickness() + kRowGap);
}

void RadialPrompt::setDisc(const QPointF &center, qreal radius)
{
    m_center = center;
    m_radius = radius;
    if (isVisible())
        layoutControls();
}

void RadialPrompt::reset(bool modal)
{
    m_modal = modal;
    m_messages.clear();
    m_controls.clear();
    m_focus = -1;
    m_hover = -1;
    m_cancelId = -1;
}

void RadialPrompt::addMessage(const QString &text)
{
    m_messages.append(text);
}

int RadialPrompt::addField(const QString &label, const QString &text, bool digitsOnly)
{
    Control c;
    c.kind = Kind::Field;
    c.label = label;
    c.text = text;
    c.digitsOnly = digitsOnly;
    c.anchor = 0;
    c.cursor = int(text.size());
    m_controls.append(c);
    return int(m_controls.size()) - 1;
}

int RadialPrompt::addToggle(const QString &label, bool on)
{
    Control c;
    c.kind = Kind::Toggle;
    c.label = label;
    c.on = on;
    m_controls.append(c);
    return int(m_controls.size()) - 1;
}

void RadialPrompt::addButton(const QString &label, int id, bool isDefault)
{
    Control c;
    c.kind = Kind::Button;
    c.label = label;
    c.id = id;
    c.isDefault = isDefault;
    m_controls.append(c);
}

QString RadialPrompt::fieldText(int field) const
{
    return m_controls.value(field).text;
}

bool RadialPrompt::toggleState(int toggle) const
{
    return m_controls.value(toggle).on;
}

int RadialPrompt::defaultButton() const
{
    for (int i = 0; i < m_controls.size(); ++i) {
        if (m_controls.at(i).kind == Kind::Button && m_controls.at(i).isDefault)
            return i;
    }
    return -1;
}

qreal RadialPrompt::depth() const
{
    int top = int(m_messages.size());
    int bottomButtons = 0;
    int bottomToggles = 0;
    for (const Control &c : m_controls) {
        if (c.kind == Kind::Field)
            ++top;
        else if (c.kind == Kind::Button)
            bottomButtons = 1;
        else
            bottomToggles = 1;
    }
    const int rows = qMax(top, bottomButtons + bottomToggles);
    return rows == 0 ? 0 : m_radius - rowInner(rows - 1) + 6;
}

void RadialPrompt::layoutControls()
{
    const QFontMetricsF metrics(font());
    int topRow = int(m_messages.size());

    QList<int> buttons;
    QList<int> toggles;
    for (int i = 0; i < m_controls.size(); ++i) {
        Control &c = m_controls[i];
        if (c.kind == Kind::Field) {
            c.row = topRow++;
            c.top = true;
            const qreal mid = rowMid(c.row);
            c.labelSweep = (Arc::advance(metrics, c.label) + 14) / mid;
            c.sweep = qMin(kFieldSweep, Arc::kPi - c.labelSweep - 0.1);
            const qreal total = c.labelSweep + c.sweep;
            c.labelFrom = Arc::kNoon - total / 2;
            c.from = c.labelFrom + c.labelSweep;
        } else if (c.kind == Kind::Button) {
            buttons.append(i);
        } else {
            toggles.append(i);
        }
    }

    // Along the bottom the text reads upright, which is right to left in
    // angle, so the first control takes the largest angle.
    auto placeRow = [&](const QList<int> &indices, int row) {
        const qreal mid = rowMid(row);
        qreal total = 0;
        for (const int i : indices) {
            Control &c = m_controls[i];
            const qreal extra = c.kind == Kind::Toggle ? 18 : 0;
            c.sweep = (Arc::advance(metrics, c.label) + 2 * kPad + extra) / mid;
            total += c.sweep;
        }
        total += kGap / mid * qMax<qsizetype>(0, indices.size() - 1);
        qreal angle = Arc::kSix + total / 2;
        for (const int i : indices) {
            Control &c = m_controls[i];
            c.row = row;
            c.top = false;
            c.from = angle - c.sweep;
            angle = c.from - kGap / mid;
        }
    };
    placeRow(buttons, 0);
    placeRow(toggles, buttons.isEmpty() ? 0 : 1);

    const int r = qCeil(m_radius) + 1;
    const QRegion disc(QRect(qRound(m_center.x()) - r, qRound(m_center.y()) - r, 2 * r, 2 * r),
                       QRegion::Ellipse);
    if (m_modal) {
        setMask(disc);
    } else {
        // Only the bands take input; the text between them stays live.
        QRegion region;
        auto add = [&](qreal inner, qreal outer, qreal from, qreal sweep) {
            const QPainterPath path = Arc::sector(m_center, inner - 2, outer + 2, from - 0.02, sweep + 0.04);
            region += QRegion(path.toFillPolygon().toPolygon());
        };
        for (int m = 0; m < m_messages.size(); ++m) {
            const qreal sweep = (Arc::advance(QFontMetricsF(font(true)), m_messages.at(m)) + 2 * kPad) / rowMid(m);
            add(rowInner(m), rowOuter(m), Arc::kNoon - sweep / 2, sweep);
        }
        for (const Control &c : m_controls) {
            if (c.kind == Kind::Field)
                add(rowInner(c.row), rowOuter(c.row), c.labelFrom, c.labelSweep + c.sweep);
            else
                add(rowInner(c.row), rowOuter(c.row), c.from, c.sweep);
        }
        setMask(region);
    }
    update();
}

void RadialPrompt::present()
{
    layoutControls();
    show();
    raise();
    int first = -1;
    for (int i = 0; i < m_controls.size() && first < 0; ++i) {
        if (m_controls.at(i).kind == Kind::Field)
            first = i;
    }
    focusControl(first >= 0 ? first : defaultButton());
    setFocus(Qt::OtherFocusReason);
    activateWindow();
}

int RadialPrompt::exec()
{
    if (m_loop)
        return m_cancelId;
    present();
    QEventLoop loop;
    m_loop = &loop;
    m_result = m_cancelId;
    loop.exec(QEventLoop::DialogExec);
    m_loop = nullptr;
    hide();
    m_blink.stop();
    Q_EMIT dismissed();
    return m_result;
}

void RadialPrompt::dismiss()
{
    if (m_loop) {
        m_result = m_cancelId;
        m_loop->quit();
        return;
    }
    if (!isVisible())
        return;
    hide();
    m_blink.stop();
    Q_EMIT dismissed();
}

void RadialPrompt::click(int id)
{
    if (m_loop) {
        m_result = id;
        m_loop->quit();
        return;
    }
    Q_EMIT buttonClicked(id);
}

void RadialPrompt::focusControl(int index)
{
    m_focus = index;
    if (RadialPrompt::Control *field = focusedField()) {
        field->anchor = 0;
        field->cursor = int(field->text.size());
        ensureCursorVisible(*field);
    }
    m_cursorOn = true;
    m_blink.start(QApplication::cursorFlashTime() / 2, this);
    update();
}

void RadialPrompt::moveFocus(int direction)
{
    const int count = int(m_controls.size());
    if (count == 0)
        return;
    focusControl(((m_focus < 0 ? 0 : m_focus) + direction + count) % count);
}

RadialPrompt::Control *RadialPrompt::focusedField()
{
    if (m_focus < 0 || m_focus >= m_controls.size() || m_controls.at(m_focus).kind != Kind::Field)
        return nullptr;
    return &m_controls[m_focus];
}

// ---------------------------------------------------------------------------
// Field text

qreal RadialPrompt::textX(const Control &field, int index) const
{
    return QFontMetricsF(font()).horizontalAdvance(field.text.left(index));
}

qreal RadialPrompt::fieldTextWidth(const Control &field) const
{
    return field.sweep * rowMid(field.row) - 2 * kPad;
}

int RadialPrompt::indexAtX(const Control &field, qreal x) const
{
    int best = 0;
    qreal bestDistance = qAbs(x);
    for (qsizetype i = nextBoundary(field.text, 0); i <= field.text.size();) {
        const qreal d = qAbs(textX(field, int(i)) - x);
        if (d < bestDistance) {
            bestDistance = d;
            best = int(i);
        }
        if (i == field.text.size())
            break;
        i = nextBoundary(field.text, i);
    }
    return best;
}

void RadialPrompt::ensureCursorVisible(Control &field)
{
    const qreal x = textX(field, field.cursor);
    const qreal width = fieldTextWidth(field);
    if (x - field.scroll > width)
        field.scroll = x - width;
    if (x < field.scroll)
        field.scroll = x;
    field.scroll = qMax<qreal>(0, field.scroll);
}

void RadialPrompt::insertIntoField(Control &field, const QString &text)
{
    QString clean = text;
    const qsizetype newline = clean.indexOf(u'\n');
    if (newline >= 0)
        clean.truncate(newline);
    clean.remove(u'\r');
    if (field.digitsOnly) {
        QString digits;
        for (const QChar ch : clean) {
            if (ch.isDigit())
                digits += ch;
        }
        clean = digits;
    }
    const int start = qMin(field.cursor, field.anchor);
    const int end = qMax(field.cursor, field.anchor);
    field.text.replace(start, end - start, clean);
    field.cursor = field.anchor = start + int(clean.size());
    ensureCursorVisible(field);
}

// ---------------------------------------------------------------------------
// Input

bool RadialPrompt::event(QEvent *event)
{
    // Keep typing inside the prompt: Ctrl+A here selects the field, not the
    // document behind it. Function keys still reach the window, so F3 works
    // with Find open.
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        const bool ctrl = key->modifiers() & Qt::ControlModifier;
        const bool alt = key->modifiers() & Qt::AltModifier;
        // While a modal answer is awaited, no window shortcut may run: Ctrl+V
        // would edit the document behind "Save changes?", and anything that
        // opens another prompt would re-enter this one.
        bool claim = m_loop != nullptr;
        switch (key->key()) {
        case Qt::Key_Escape: case Qt::Key_Tab: case Qt::Key_Backtab: case Qt::Key_Return:
        case Qt::Key_Enter: case Qt::Key_Space: case Qt::Key_Left: case Qt::Key_Right:
        case Qt::Key_Home: case Qt::Key_End: case Qt::Key_Backspace: case Qt::Key_Delete:
            claim = claim || !alt;
            break;
        case Qt::Key_A: case Qt::Key_C: case Qt::Key_X: case Qt::Key_V: case Qt::Key_Z: case Qt::Key_Y:
            claim = claim || (ctrl && focusedField());
            break;
        case Qt::Key_F5:
            // Time/Date would go into the document, not the field being typed in.
            claim = claim || focusedField();
            break;
        default:
            break;
        }
        if (!claim && !ctrl && !alt && !key->text().isEmpty() && key->text().at(0).isPrint())
            claim = true;
        if (claim) {
            key->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

void RadialPrompt::keyPressEvent(QKeyEvent *event)
{
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const bool ctrl = event->modifiers() & Qt::ControlModifier;

    switch (event->key()) {
    case Qt::Key_Escape:
        if (m_cancelId >= 0)
            click(m_cancelId);
        else
            dismiss();
        return;
    case Qt::Key_Tab:
        moveFocus(1);
        return;
    case Qt::Key_Backtab:
        moveFocus(-1);
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        if (m_focus >= 0 && m_controls.at(m_focus).kind == Kind::Button) {
            click(m_controls.at(m_focus).id);
        } else if (m_focus >= 0 && m_controls.at(m_focus).kind == Kind::Toggle) {
            m_controls[m_focus].on = !m_controls.at(m_focus).on;
            update();
        } else if (defaultButton() >= 0) {
            click(m_controls.at(defaultButton()).id);
        }
        return;
    }
    default:
        break;
    }

    if (Control *field = focusedField()) {
        const int length = int(field->text.size());
        const int selStart = qMin(field->cursor, field->anchor);
        const int selEnd = qMax(field->cursor, field->anchor);
        const auto moveTo = [&](int position) {
            field->cursor = position;
            if (!shift)
                field->anchor = position;
            ensureCursorVisible(*field);
        };

        if (ctrl && event->key() == Qt::Key_A) {
            field->anchor = 0;
            field->cursor = length;
        } else if (ctrl && (event->key() == Qt::Key_C || event->key() == Qt::Key_X)) {
            if (selStart < selEnd) {
                QGuiApplication::clipboard()->setText(field->text.mid(selStart, selEnd - selStart));
                if (event->key() == Qt::Key_X)
                    insertIntoField(*field, QString());
            }
        } else if (ctrl && event->key() == Qt::Key_V) {
            insertIntoField(*field, QGuiApplication::clipboard()->text());
        } else if (event->key() == Qt::Key_Left) {
            moveTo(!shift && selStart < selEnd ? selStart : int(previousBoundary(field->text, field->cursor)));
        } else if (event->key() == Qt::Key_Right) {
            moveTo(!shift && selStart < selEnd ? selEnd : int(nextBoundary(field->text, field->cursor)));
        } else if (event->key() == Qt::Key_Home) {
            moveTo(0);
        } else if (event->key() == Qt::Key_End) {
            moveTo(length);
        } else if (event->key() == Qt::Key_Backspace) {
            if (selStart == selEnd)
                field->anchor = int(previousBoundary(field->text, field->cursor));
            insertIntoField(*field, QString());
        } else if (event->key() == Qt::Key_Delete) {
            if (selStart == selEnd)
                field->anchor = int(nextBoundary(field->text, field->cursor));
            insertIntoField(*field, QString());
        } else if (!ctrl && !event->text().isEmpty() && event->text().at(0).isPrint()) {
            insertIntoField(*field, event->text());
        } else {
            event->ignore();
            return;
        }
        m_cursorOn = true;
        m_blink.start(QApplication::cursorFlashTime() / 2, this);
        update();
        return;
    }

    if (m_focus >= 0) {
        Control &c = m_controls[m_focus];
        if (event->key() == Qt::Key_Space) {
            if (c.kind == Kind::Toggle) {
                c.on = !c.on;
                update();
            } else if (c.kind == Kind::Button) {
                click(c.id);
            }
            return;
        }
        // Bottom-row text reads right to left in angle, so Left is the
        // previous control and Right the next, as they appear on screen.
        if (event->key() == Qt::Key_Left) {
            moveFocus(-1);
            return;
        }
        if (event->key() == Qt::Key_Right) {
            moveFocus(1);
            return;
        }
    }
    event->ignore();
}

void RadialPrompt::inputMethodEvent(QInputMethodEvent *event)
{
    if (Control *field = focusedField()) {
        if (!event->commitString().isEmpty()) {
            insertIntoField(*field, event->commitString());
            update();
        }
    }
    event->accept();
}

QVariant RadialPrompt::inputMethodQuery(Qt::InputMethodQuery query) const
{
    const Control *field = (m_focus >= 0 && m_focus < m_controls.size()
                            && m_controls.at(m_focus).kind == Kind::Field)
        ? &m_controls.at(m_focus)
        : nullptr;
    switch (query) {
    case Qt::ImEnabled:
        return field != nullptr;
    case Qt::ImCursorRectangle: {
        if (!field)
            return QRectF();
        const qreal mid = rowMid(field->row);
        const qreal angle = field->from + (kPad + textX(*field, field->cursor) - field->scroll) / mid;
        const QPointF at = Arc::polar(m_center, mid, angle);
        return QRectF(at.x() - 1, at.y() - 8, 2, 16);
    }
    default:
        return QWidget::inputMethodQuery(query);
    }
}

void RadialPrompt::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_blink.timerId()) {
        m_cursorOn = !m_cursorOn;
        update();
        return;
    }
    QWidget::timerEvent(event);
}

void RadialPrompt::focusInEvent(QFocusEvent *event)
{
    m_cursorOn = true;
    m_blink.start(QApplication::cursorFlashTime() / 2, this);
    update();
    QWidget::focusInEvent(event);
}

void RadialPrompt::focusOutEvent(QFocusEvent *event)
{
    m_blink.stop();
    m_cursorOn = false;
    update();
    QWidget::focusOutEvent(event);
}

int RadialPrompt::controlAt(const QPointF &pos) const
{
    const qreal distance = QLineF(m_center, pos).length();
    const qreal angle = Arc::angleOf(m_center, pos);
    for (int i = 0; i < m_controls.size(); ++i) {
        const Control &c = m_controls.at(i);
        if (distance < rowInner(c.row) - 2 || distance > rowOuter(c.row) + 2)
            continue;
        const qreal from = c.kind == Kind::Field ? c.labelFrom : c.from;
        const qreal sweep = c.kind == Kind::Field ? c.labelSweep + c.sweep : c.sweep;
        if (Arc::within(angle, from, sweep))
            return i;
    }
    return -1;
}

void RadialPrompt::mouseMoveEvent(QMouseEvent *event)
{
    const int hover = controlAt(event->position());
    if (hover != m_hover) {
        m_hover = hover;
        update();
    }
    if (hover < 0)
        setCursor(Qt::ArrowCursor);
    else
        setCursor(m_controls.at(hover).kind == Kind::Field ? Qt::IBeamCursor : Qt::PointingHandCursor);
}

void RadialPrompt::mousePressEvent(QMouseEvent *event)
{
    const int index = controlAt(event->position());
    if (index < 0)
        return;
    Control &c = m_controls[index];
    switch (c.kind) {
    case Kind::Field: {
        m_focus = index;
        const qreal mid = rowMid(c.row);
        const qreal along = Arc::wrap(Arc::angleOf(m_center, event->position()) - c.from) * mid - kPad + c.scroll;
        c.cursor = indexAtX(c, along);
        if (!(event->modifiers() & Qt::ShiftModifier))
            c.anchor = c.cursor;
        m_cursorOn = true;
        m_blink.start(QApplication::cursorFlashTime() / 2, this);
        update();
        break;
    }
    case Kind::Toggle:
        m_focus = index;
        c.on = !c.on;
        update();
        break;
    case Kind::Button:
        m_focus = index;
        update();
        click(c.id);
        break;
    }
}

// ---------------------------------------------------------------------------
// Painting

void RadialPrompt::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    if (m_modal) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_colors.shade);
        p.drawEllipse(m_center, m_radius, m_radius);
    }

    const QFont plain = font();
    const QFont bold = font(true);
    const QFontMetricsF metrics(plain);

    const auto band = [&](int row, qreal from, qreal sweep, const QColor &fill, const QColor &edge, qreal width) {
        const QPainterPath path = Arc::sector(m_center, rowInner(row), rowOuter(row), from, sweep);
        p.setPen(width > 0 ? QPen(edge, width) : Qt::NoPen);
        p.setBrush(fill);
        p.drawPath(path);
    };

    for (int m = 0; m < m_messages.size(); ++m) {
        const qreal mid = rowMid(m);
        const qreal sweep = (Arc::advance(QFontMetricsF(bold), m_messages.at(m)) + 2 * kPad) / mid;
        band(m, Arc::kNoon - sweep / 2, sweep, m_colors.band, m_colors.bandEdge, 1);
        Arc::drawCentered(p, m_center, mid, Arc::kNoon, m_messages.at(m), bold, m_colors.ink);
    }

    for (int i = 0; i < m_controls.size(); ++i) {
        const Control &c = m_controls.at(i);
        const bool focused = i == m_focus && hasFocus();
        const bool hovered = i == m_hover;
        const qreal mid = rowMid(c.row);

        if (c.kind == Kind::Field) {
            band(c.row, c.labelFrom, c.labelSweep + c.sweep, m_colors.band, m_colors.bandEdge, 1);
            Arc::drawFrom(p, m_center, mid, c.labelFrom + 7 / mid, c.label, plain, m_colors.dim, false);
            band(c.row, c.from, c.sweep, m_colors.field, focused ? m_colors.accent : m_colors.bandEdge,
                 focused ? 1.8 : 1);

            const qreal textFrom = c.from + kPad / mid;
            const qreal visible = fieldTextWidth(c);
            const int selStart = qMin(c.cursor, c.anchor);
            const int selEnd = qMax(c.cursor, c.anchor);
            if (focused && selStart < selEnd) {
                const qreal x0 = qBound<qreal>(0, textX(c, selStart) - c.scroll, visible);
                const qreal x1 = qBound<qreal>(0, textX(c, selEnd) - c.scroll, visible);
                const QPainterPath path = Arc::sector(m_center, rowInner(c.row) + 4, rowOuter(c.row) - 4,
                                                      textFrom + x0 / mid, (x1 - x0) / mid);
                p.fillPath(path, m_colors.selection);
            }
            for (qsizetype at = 0; at < c.text.size();) {
                const qsizetype next = nextBoundary(c.text, at);
                const qreal x0 = textX(c, int(at)) - c.scroll;
                const qreal x1 = textX(c, int(next)) - c.scroll;
                if (x0 >= -0.5 && x1 <= visible + 0.5)
                    Arc::drawFrom(p, m_center, mid, textFrom + x0 / mid, c.text.mid(at, next - at), plain,
                                  m_colors.ink, false);
                at = next;
            }
            if (focused && m_cursorOn) {
                const qreal x = textX(c, c.cursor) - c.scroll;
                const qreal angle = textFrom + x / mid;
                p.setPen(QPen(m_colors.ink, 1.5));
                p.drawLine(Arc::polar(m_center, mid - metrics.ascent() * 0.62, angle),
                           Arc::polar(m_center, mid + metrics.ascent() * 0.62, angle));
            }
            continue;
        }

        QColor fill = m_colors.band;
        QColor ink = m_colors.ink;
        if (c.kind == Kind::Button && c.isDefault) {
            fill = m_colors.accent;
            ink = m_colors.accentInk;
        }
        if (hovered)
            fill = fill.lighter(c.isDefault ? 112 : 106);
        band(c.row, c.from, c.sweep, fill, focused ? (c.isDefault ? m_colors.ink : m_colors.accent) : m_colors.bandEdge,
             focused ? 1.8 : 1);

        if (c.kind == Kind::Button) {
            Arc::drawCentered(p, m_center, mid, c.from + c.sweep / 2, c.label, plain, ink);
        } else {
            // A toggle: a small ring where reading starts, filled when on.
            const qreal dot = c.from + c.sweep - (kPad + 4) / mid;
            const QPointF at = Arc::polar(m_center, mid, dot);
            p.setPen(QPen(ink, 1.3));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(at, 5, 5);
            if (c.on) {
                p.setPen(Qt::NoPen);
                p.setBrush(m_colors.accent);
                p.drawEllipse(at, 3, 3);
            }
            Arc::drawFrom(p, m_center, mid, c.from + c.sweep - (kPad + 14) / mid, c.label, plain, ink, true);
        }
    }
}
