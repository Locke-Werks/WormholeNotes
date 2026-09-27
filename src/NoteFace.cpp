#include "NoteFace.h"

#include "Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleHints>
#include <QTextBlock>
#include <QTextDocument>
#include <QVariantAnimation>

#include <cmath>
#include <numbers>

namespace {

constexpr int kBlinkMs = 530;

} // namespace

NoteFace::NoteFace(QWidget *parent)
    : QWidget(parent)
    , m_doc(new QTextDocument(this))
    , m_cursor(m_doc)
{
    setAttribute(Qt::WA_InputMethodEnabled);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::IBeamCursor);
    m_doc->setUndoRedoEnabled(true);
    connect(m_doc, &QTextDocument::contentsChanged, this, [this] {
        relayout();
        cursorMoved();
    });
    m_spin = new QVariantAnimation(this);
    m_spin->setDuration(260);
    m_spin->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_spin, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_turn = value.toReal();
        update();
    });
}

void NoteFace::spinToCaret(bool animate)
{
    constexpr qreal pi = std::numbers::pi;
    // Turned so the caret lands where the newest writing rests.
    qreal target = SpiralLayout::anchor() - m_layout.screenAngle(m_cursor.position());
    target = std::remainder(target, 2 * pi);
    // The short way round from wherever the face is now.
    const qreal from = m_spin->state() == QAbstractAnimation::Running ? m_turn : m_turn;
    target = from + std::remainder(target - from, 2 * pi);
    m_spin->stop();
    if (!animate || std::abs(target - from) < 0.002) {
        m_turn = target;
        update();
        return;
    }
    m_spin->setStartValue(from);
    m_spin->setEndValue(target);
    m_spin->start();
}

QPointF NoteFace::unturned(const QPointF &pos) const
{
    const qreal r = m_layout.diameter() / 2;
    const QPointF v = pos - QPointF(r, r);
    const qreal c = std::cos(-m_turn), s = std::sin(-m_turn);
    return QPointF(r, r) + QPointF(v.x() * c - v.y() * s, v.x() * s + v.y() * c);
}

QString NoteFace::text() const
{
    // toPlainText turns non-breaking spaces into ordinary ones; the blocks
    // hold exactly what was typed.
    QStringList lines;
    for (QTextBlock block = m_doc->begin(); block.isValid(); block = block.next())
        lines.append(block.text());
    return lines.join(u'\n');
}

void NoteFace::load(const QString &text)
{
    const QSignalBlocker blocker(m_doc);
    m_doc->setPlainText(text);
    m_doc->clearUndoRedoStacks();
    relayout();
    m_cursor = QTextCursor(m_doc);
    m_cursor.movePosition(QTextCursor::End);
    m_page = m_layout.pageCount() - 1;
    m_hasGoal = false;
    spinToCaret(false);
    update();
}

void NoteFace::selectRange(int start, int length)
{
    const int end = m_doc->characterCount() - 1;
    m_cursor.setPosition(qBound(0, start, end));
    m_cursor.setPosition(qBound(0, start + length, end), QTextCursor::KeepAnchor);
    m_hasGoal = false;
    cursorMoved();
}

void NoteFace::setSheetMarker(int index, int count)
{
    m_sheetIndex = index;
    m_sheetCount = qMax(1, count);
    update();
}

bool NoteFace::canTurn(int direction) const
{
    if (direction < 0)
        return m_page > 0 || m_sheetIndex > 0;
    // Past the last sheet a new one starts, unless this one is still blank.
    return m_page < m_layout.pageCount() - 1 || m_sheetIndex < m_sheetCount - 1 || !m_doc->isEmpty();
}

void NoteFace::turn(int direction)
{
    const int page = m_page + direction;
    if (page >= 0 && page < m_layout.pageCount()) {
        showPage(page);
        // The cursor goes with the page, to its newest end, at the top.
        m_cursor.setPosition(m_layout.lastOf(page));
        m_hasGoal = false;
        spinToCaret();
        update();
    } else if (canTurn(direction)) {
        emit sheetTurnRequested(direction);
    }
}

void NoteFace::setColors(const Colors &colors)
{
    m_colors = colors;
    update();
}

void NoteFace::setTextFont(const QFont &font)
{
    m_layout.setFont(font);
    relayout();
    cursorMoved();
}

void NoteFace::relayout()
{
    m_layout.setDiameter(qMin(width(), height()));
    m_layout.layout(m_doc);
    m_page = qBound(0, m_page, m_layout.pageCount() - 1);
    update();
}

void NoteFace::resizeEvent(QResizeEvent *)
{
    // Only the circle is the face. The square's corners lie over the rim,
    // and clicks there belong to the pushers and the clip.
    const int d = qMin(width(), height());
    setMask(QRegion(0, 0, d, d, QRegion::Ellipse));
    relayout();
    showPage(m_layout.pageOf(m_cursor.position()));
}

void NoteFace::showPage(int page)
{
    page = qBound(0, page, m_layout.pageCount() - 1);
    if (page != m_page) {
        m_page = page;
        update();
    }
}

void NoteFace::cursorMoved(bool keepGoal)
{
    if (!keepGoal)
        m_hasGoal = false;
    showPage(m_layout.pageOf(m_cursor.position()));
    // Not while a selection is being dragged out: the text would move under
    // the pointer. The face turns when the button comes up.
    if (!m_selecting)
        spinToCaret();
    m_caretOn = true;
    if (hasFocus())
        m_blink.start(kBlinkMs, this);
    QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
    update();
}

// ---------------------------------------------------------------------------
// Painting

QRectF NoteFace::controlRect(Control control) const
{
    // Either side of the middle, where the spiral ends.
    const qreal r = m_layout.diameter() / 2;
    const qreal hub = m_layout.hubRadius();
    const qreal size = qMax(16.0, hub * 0.42);
    const qreal x = control == Back ? r - hub * 0.62 : r + hub * 0.62;
    return QRectF(x - size / 2, r - size / 2, size, size);
}

NoteFace::Control NoteFace::controlAt(const QPointF &pos) const
{
    for (const Control c : { Back, On }) {
        if (controlRect(c).adjusted(-4, -4, 4, 4).contains(pos))
            return c;
    }
    return NoControl;
}

void NoteFace::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal r = m_layout.diameter() / 2;
    const QPointF c(r, r);

    // Paper, a shade deeper towards the rim.
    QRadialGradient paper(c, r);
    paper.setColorAt(0, m_colors.paper);
    paper.setColorAt(0.82, m_colors.paper);
    paper.setColorAt(1, m_colors.paperEdge);
    p.setPen(Qt::NoPen);
    p.setBrush(paper);
    p.drawEllipse(c, r, r);

    QPainterPath circle;
    circle.addEllipse(c, r, r);
    p.setClipPath(circle);

    // The bloom: the sheet's colour, faint, as if lit from above the rim.
    QRadialGradient bloom(c.x(), -r * 0.2, r * 1.3);
    bloom.setColorAt(0, m_colors.bloom);
    bloom.setColorAt(1, Theme::withAlpha(m_colors.bloom, 0));
    p.fillRect(QRectF(0, 0, 2 * r, 2 * r), bloom);

    // The groove the writing follows, faint, like the rule on a card; and the
    // writing, turned with the face.
    p.save();
    p.translate(c);
    p.rotate(m_turn * 180 / std::numbers::pi);
    p.translate(-c);
    m_layout.drawGroove(p, m_colors.rule);
    m_layout.draw(p, m_page, m_colors.ink, m_colors.selection, m_colors.control, m_cursor.selectionStart(),
                  m_cursor.selectionEnd());
    if (hasFocus() && m_caretOn && m_layout.pageOf(m_cursor.position()) == m_page) {
        p.setPen(QPen(m_colors.caret, 2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(m_layout.caret(m_cursor.position()));
    }
    p.restore();

    // The middle: which sheet, which page, and the two ways to turn.
    const bool pages = m_layout.pageCount() > 1;
    const bool sheets = m_sheetCount > 1;
    if (!(pages || sheets || canTurn(1)))
        return;
    const qreal hub = m_layout.hubRadius();
    QFont font = Theme::labelFont(qMax(10.0, hub * 0.22));
    p.setFont(font);
    p.setPen(m_colors.control);
    const QRectF back = controlRect(Back);
    const QRectF on = controlRect(On);
    const QRectF marker(back.right(), c.y() - hub * 0.2, on.left() - back.right(), hub * 0.4);
    p.drawText(marker, Qt::AlignCenter, QStringLiteral("%1/%2").arg(m_sheetIndex + 1).arg(m_sheetCount));
    if (pages) {
        font = Theme::labelFont(qMax(8.0, hub * 0.13));
        p.setFont(font);
        p.drawText(QRectF(c.x() - hub, c.y() + hub * 0.2, 2 * hub, hub * 0.3), Qt::AlignCenter,
                   tr("page %1 of %2").arg(m_page + 1).arg(m_layout.pageCount()));
    }
    for (const Control control : { Back, On }) {
        const QRectF box = controlRect(control);
        const bool enabled = canTurn(control == Back ? -1 : 1);
        if (enabled && m_hover == control) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_colors.controlHot);
            p.drawEllipse(box.center(), box.width() / 2, box.width() / 2);
        }
        QColor ink = m_colors.control;
        if (!enabled)
            ink.setAlpha(55);
        const qreal s = box.width() * 0.2;
        const qreal dir = control == Back ? -1 : 1;
        p.setPen(QPen(ink, qMax(1.4, box.width() * 0.09), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        const QPointF m = box.center();
        // A last sheet with writing on it turns on to a new one: a plus.
        if (control == On && enabled && m_page == m_layout.pageCount() - 1 && m_sheetIndex == m_sheetCount - 1) {
            p.drawLine(m + QPointF(-s, 0), m + QPointF(s, 0));
            p.drawLine(m + QPointF(0, -s), m + QPointF(0, s));
        } else {
            p.drawPolyline(QPolygonF({ m + QPointF(-dir * s * 0.6, -s), m + QPointF(dir * s * 0.6, 0),
                                       m + QPointF(-dir * s * 0.6, s) }));
        }
    }
}

// ---------------------------------------------------------------------------
// Editing

void NoteFace::insert(const QString &text)
{
    m_cursor.insertText(text);
    cursorMoved();
}

void NoteFace::undo()
{
    m_doc->undo(&m_cursor);
    cursorMoved();
}

void NoteFace::redo()
{
    m_doc->redo(&m_cursor);
    cursorMoved();
}

void NoteFace::cut()
{
    if (!m_cursor.hasSelection())
        return;
    copy();
    m_cursor.removeSelectedText();
    cursorMoved();
}

void NoteFace::copy()
{
    if (m_cursor.hasSelection())
        QGuiApplication::clipboard()->setText(m_cursor.selectedText().replace(QChar::ParagraphSeparator, u'\n'));
}

void NoteFace::paste()
{
    QString text = QGuiApplication::clipboard()->text();
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    if (!text.isEmpty())
        insert(text);
}

void NoteFace::selectAll()
{
    m_cursor.select(QTextCursor::Document);
    cursorMoved();
}

void NoteFace::deleteSelection()
{
    m_cursor.removeSelectedText();
    cursorMoved();
}

// Up and down move a turn out or in along the radius, keeping the angle they
// started from so a short run in between does not pull them round.
void NoteFace::moveTurns(int delta, QTextCursor::MoveMode mode)
{
    const int position = m_cursor.position();
    if (!m_hasGoal) {
        m_goalAngle = m_layout.angleOf(position);
        m_hasGoal = true;
    }
    int page = m_layout.pageOf(position);
    int turn = m_layout.turnOf(position) + delta;
    if (turn < 0) {
        if (page == 0) {
            m_cursor.movePosition(QTextCursor::Start, mode);
            cursorMoved(true);
            return;
        }
        --page;
        turn = m_layout.turnCount(page) - 1;
    } else if (turn >= m_layout.turnCount(page)) {
        if (page == m_layout.pageCount() - 1) {
            m_cursor.movePosition(QTextCursor::End, mode);
            cursorMoved(true);
            return;
        }
        ++page;
        turn = 0;
    }
    m_cursor.setPosition(m_layout.positionOnTurn(page, turn, m_goalAngle), mode);
    cursorMoved(true);
}

void NoteFace::keyPressEvent(QKeyEvent *event)
{
    const auto mode = event->modifiers() & Qt::ShiftModifier ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor;
    const bool ctrl = event->modifiers() & Qt::ControlModifier;

    if (event->matches(QKeySequence::Undo)) { undo(); return; }
    if (event->matches(QKeySequence::Redo) || (ctrl && event->key() == Qt::Key_Y)) { redo(); return; }
    if (event->matches(QKeySequence::Cut)) { cut(); return; }
    if (event->matches(QKeySequence::Copy)) { copy(); return; }
    if (event->matches(QKeySequence::Paste)) { paste(); return; }
    if (event->matches(QKeySequence::SelectAll)) { selectAll(); return; }

    const int position = m_cursor.position();
    const int page = m_layout.pageOf(position);
    const int onTurn = m_layout.turnOf(position);
    switch (event->key()) {
    case Qt::Key_Left:
        m_cursor.movePosition(ctrl ? QTextCursor::PreviousWord : QTextCursor::PreviousCharacter, mode);
        cursorMoved();
        return;
    case Qt::Key_Right:
        m_cursor.movePosition(ctrl ? QTextCursor::NextWord : QTextCursor::NextCharacter, mode);
        cursorMoved();
        return;
    case Qt::Key_Up:
        moveTurns(-1, mode);
        return;
    case Qt::Key_Down:
        moveTurns(1, mode);
        return;
    case Qt::Key_Home:
        if (ctrl)
            m_cursor.movePosition(QTextCursor::Start, mode);
        else
            m_cursor.setPosition(m_layout.turnStart(page, onTurn), mode);
        cursorMoved();
        return;
    case Qt::Key_End:
        if (ctrl)
            m_cursor.movePosition(QTextCursor::End, mode);
        else
            m_cursor.setPosition(m_layout.turnEnd(page, onTurn), mode);
        cursorMoved();
        return;
    case Qt::Key_PageUp:
        turn(-1);
        return;
    case Qt::Key_PageDown:
        turn(1);
        return;
    case Qt::Key_Backspace:
        if (!m_cursor.hasSelection())
            m_cursor.movePosition(ctrl ? QTextCursor::PreviousWord : QTextCursor::PreviousCharacter,
                                  QTextCursor::KeepAnchor);
        m_cursor.removeSelectedText();
        cursorMoved();
        return;
    case Qt::Key_Delete:
        if (!m_cursor.hasSelection())
            m_cursor.movePosition(ctrl ? QTextCursor::NextWord : QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        m_cursor.removeSelectedText();
        cursorMoved();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        m_cursor.insertBlock();
        cursorMoved();
        return;
    case Qt::Key_Tab:
        insert(QStringLiteral("\t"));
        return;
    default:
        break;
    }

    const QString text = event->text();
    if (!text.isEmpty() && !ctrl && text.at(0).isPrint()) {
        insert(text);
        return;
    }
    QWidget::keyPressEvent(event);
}

void NoteFace::inputMethodEvent(QInputMethodEvent *event)
{
    if (!event->commitString().isEmpty())
        insert(event->commitString());
    event->accept();
}

QVariant NoteFace::inputMethodQuery(Qt::InputMethodQuery query) const
{
    switch (query) {
    case Qt::ImCursorRectangle: {
        QTransform turn;
        const qreal r = m_layout.diameter() / 2;
        turn.translate(r, r).rotateRadians(m_turn).translate(-r, -r);
        const QLineF caret = turn.map(m_layout.caret(m_cursor.position()));
        return QRectF(caret.p1(), caret.p2()).normalized().adjusted(-1, -1, 1, 1).toRect();
    }
    case Qt::ImFont:
        return font();
    case Qt::ImCursorPosition:
        return m_cursor.positionInBlock();
    case Qt::ImSurroundingText:
        return m_cursor.block().text();
    case Qt::ImEnabled:
        return true;
    default:
        return QWidget::inputMethodQuery(query);
    }
}

// ---------------------------------------------------------------------------
// Mouse

void NoteFace::mousePressEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    if (const Control control = controlAt(pos); control != NoControl) {
        if (event->button() == Qt::LeftButton)
            turn(control == Back ? -1 : 1);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    const int position = m_layout.positionAt(m_page, unturned(pos));

    if (event->button() == Qt::RightButton) {
        if (position < m_cursor.selectionStart() || position > m_cursor.selectionEnd()) {
            m_cursor.setPosition(position);
            cursorMoved();
        }
        emit contextMenuRequested(event->globalPosition().toPoint());
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;

    // Soon after a double-click, a third click takes the whole paragraph.
    if (m_doubleClick.isValid() && m_doubleClick.elapsed() < QGuiApplication::styleHints()->mouseDoubleClickInterval()) {
        m_doubleClick.invalidate();
        m_cursor.setPosition(position);
        m_cursor.select(QTextCursor::BlockUnderCursor);
        cursorMoved();
        return;
    }
    m_cursor.setPosition(position, event->modifiers() & Qt::ShiftModifier ? QTextCursor::KeepAnchor
                                                                         : QTextCursor::MoveAnchor);
    m_selecting = true;
    cursorMoved();
}

void NoteFace::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    if (m_selecting && (event->buttons() & Qt::LeftButton)) {
        m_cursor.setPosition(m_layout.positionAt(m_page, unturned(pos)), QTextCursor::KeepAnchor);
        m_caretOn = true;
        update();
        return;
    }
    Control hover = controlAt(pos);
    if (hover != NoControl && !canTurn(hover == Back ? -1 : 1))
        hover = NoControl;
    if (hover != m_hover) {
        m_hover = hover;
        update();
    }
    setCursor(hover != NoControl ? Qt::PointingHandCursor : Qt::IBeamCursor);
}

void NoteFace::mouseReleaseEvent(QMouseEvent *)
{
    if (m_selecting) {
        m_selecting = false;
        spinToCaret();
    }
}

void NoteFace::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (controlAt(event->position()) != NoControl) {
        mousePressEvent(event);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    m_cursor.setPosition(m_layout.positionAt(m_page, unturned(event->position())));
    m_cursor.select(QTextCursor::WordUnderCursor);
    m_selecting = false;
    m_doubleClick.start();
    cursorMoved();
}

void NoteFace::leaveEvent(QEvent *)
{
    if (m_hover != NoControl) {
        m_hover = NoControl;
        update();
    }
}

void NoteFace::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (event->modifiers() & Qt::ControlModifier) {
        m_zoomWheel += delta;
        const int steps = m_zoomWheel / 120;
        if (steps != 0) {
            m_zoomWheel -= steps * 120;
            emit zoomRequested(steps);
        }
        event->accept();
        return;
    }
    // The wheel turns pages only; moving between sheets is a deliberate
    // click, so scrolling never starts a blank one.
    m_wheel += delta;
    while (m_wheel >= 120) {
        m_wheel -= 120;
        if (m_page > 0)
            turn(-1);
    }
    while (m_wheel <= -120) {
        m_wheel += 120;
        if (m_page < m_layout.pageCount() - 1)
            turn(1);
    }
    event->accept();
}

// ---------------------------------------------------------------------------
// Focus and the caret

void NoteFace::focusInEvent(QFocusEvent *)
{
    m_caretOn = true;
    m_blink.start(kBlinkMs, this);
    update();
}

void NoteFace::focusOutEvent(QFocusEvent *)
{
    m_blink.stop();
    update();
}

void NoteFace::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_blink.timerId()) {
        m_caretOn = !m_caretOn;
        update();
    }
}

bool NoteFace::event(QEvent *event)
{
    // Tab is text here, not a way to move focus.
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Tab) {
        keyPressEvent(static_cast<QKeyEvent *>(event));
        return true;
    }
    return QWidget::event(event);
}
