#include "RoundEdit.h"

#include "Arc.h"
#include "RingTextLayout.h"

#include <QApplication>
#include <QClipboard>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QStyleHints>
#include <QTextBlock>
#include <QTextDocument>
#include <QUrl>

#include <cmath>

namespace {

struct MoveBinding
{
    QKeySequence::StandardKey key;
    QTextCursor::MoveOperation operation;
    QTextCursor::MoveMode mode;
};

// Horizontal movement is Qt's; vertical movement is ours, since "the line
// above" on a ring means the next ring out at the same angle.
const MoveBinding kMoves[] = {
    { QKeySequence::MoveToNextChar, QTextCursor::NextCharacter, QTextCursor::MoveAnchor },
    { QKeySequence::MoveToPreviousChar, QTextCursor::PreviousCharacter, QTextCursor::MoveAnchor },
    { QKeySequence::MoveToNextWord, QTextCursor::NextWord, QTextCursor::MoveAnchor },
    { QKeySequence::MoveToPreviousWord, QTextCursor::PreviousWord, QTextCursor::MoveAnchor },
    { QKeySequence::MoveToStartOfLine, QTextCursor::StartOfLine, QTextCursor::MoveAnchor },
    { QKeySequence::MoveToEndOfLine, QTextCursor::EndOfLine, QTextCursor::MoveAnchor },
    { QKeySequence::MoveToStartOfDocument, QTextCursor::Start, QTextCursor::MoveAnchor },
    { QKeySequence::MoveToEndOfDocument, QTextCursor::End, QTextCursor::MoveAnchor },
    { QKeySequence::SelectNextChar, QTextCursor::NextCharacter, QTextCursor::KeepAnchor },
    { QKeySequence::SelectPreviousChar, QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor },
    { QKeySequence::SelectNextWord, QTextCursor::NextWord, QTextCursor::KeepAnchor },
    { QKeySequence::SelectPreviousWord, QTextCursor::PreviousWord, QTextCursor::KeepAnchor },
    { QKeySequence::SelectStartOfLine, QTextCursor::StartOfLine, QTextCursor::KeepAnchor },
    { QKeySequence::SelectEndOfLine, QTextCursor::EndOfLine, QTextCursor::KeepAnchor },
    { QKeySequence::SelectStartOfDocument, QTextCursor::Start, QTextCursor::KeepAnchor },
    { QKeySequence::SelectEndOfDocument, QTextCursor::End, QTextCursor::KeepAnchor },
};

const QKeySequence::StandardKey kOtherEditingKeys[] = {
    QKeySequence::MoveToNextLine, QKeySequence::MoveToPreviousLine,
    QKeySequence::MoveToNextPage, QKeySequence::MoveToPreviousPage,
    QKeySequence::SelectNextLine, QKeySequence::SelectPreviousLine,
    QKeySequence::SelectNextPage, QKeySequence::SelectPreviousPage,
    QKeySequence::Delete, QKeySequence::DeleteEndOfWord, QKeySequence::DeleteStartOfWord,
    QKeySequence::InsertParagraphSeparator, QKeySequence::InsertLineSeparator,
};

QString clipboardText(QString text)
{
    text.replace(QChar::ParagraphSeparator, u'\n');
    text.replace(QChar::LineSeparator, u'\n');
    return text;
}

bool isTyping(const QKeyEvent *event)
{
    const QString text = event->text();
    if (text.isEmpty() || !(text.at(0).isPrint() || text.at(0) == u'\t'))
        return false;
    // Ctrl alone is a shortcut; Ctrl+Alt is AltGr on many layouts and types.
    const Qt::KeyboardModifiers mods = event->modifiers() & (Qt::ControlModifier | Qt::AltModifier);
    return mods != Qt::ControlModifier && mods != Qt::AltModifier;
}

} // namespace

RoundEdit::RoundEdit(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled);
    setAcceptDrops(true);
    setMouseTracking(true);
    setCursor(Qt::IBeamCursor);

    m_doc = new QTextDocument(this);
    m_doc->setDocumentMargin(0);
    m_doc->setDefaultFont(font());
    m_layout = new RingTextLayout(m_doc);
    m_doc->setDocumentLayout(m_layout);
    m_cursor = QTextCursor(m_doc);

    connect(m_doc, &QTextDocument::contentsChanged, this, [this] {
        invalidate();
        cursorMoved(true);
    });
    connect(m_layout, &QAbstractTextDocumentLayout::update, this, &RoundEdit::invalidate);
    connect(m_layout, &QAbstractTextDocumentLayout::pageCountChanged, this, [this] {
        m_page = qBound(0, m_page, pageCount() - 1);
        invalidate();
        Q_EMIT pageChanged(m_page, pageCount());
    });
}

// ---------------------------------------------------------------------------
// Content

void RoundEdit::setTextCursor(const QTextCursor &cursor)
{
    m_cursor = cursor;
    cursorMoved();
}

void RoundEdit::moveCursor(QTextCursor::MoveOperation operation, QTextCursor::MoveMode mode)
{
    m_cursor.movePosition(operation, mode);
    cursorMoved();
}

void RoundEdit::setPlainText(const QString &text)
{
    m_doc->setPlainText(text);
    m_cursor = QTextCursor(m_doc);
    m_page = 0;
    invalidate();
    cursorMoved();
}

void RoundEdit::clear()
{
    setPlainText(QString());
}

QString RoundEdit::exactText() const
{
    QString text;
    for (QTextBlock block = m_doc->begin(); block.isValid(); block = block.next()) {
        if (block != m_doc->begin())
            text += u'\n';
        text += block.text();
    }
    return text;
}

void RoundEdit::insertPlainText(const QString &text)
{
    m_cursor.insertText(text);
    cursorMoved();
}

void RoundEdit::undo()
{
    m_doc->undo(&m_cursor);
    cursorMoved();
}

void RoundEdit::redo()
{
    m_doc->redo(&m_cursor);
    cursorMoved();
}

void RoundEdit::copy()
{
    if (m_cursor.hasSelection())
        QGuiApplication::clipboard()->setText(clipboardText(m_cursor.selectedText()));
}

void RoundEdit::cut()
{
    if (!m_cursor.hasSelection())
        return;
    copy();
    m_cursor.removeSelectedText();
    cursorMoved();
}

void RoundEdit::paste()
{
    QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty())
        return;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(u'\r', u'\n');
    m_cursor.insertText(text);
    cursorMoved();
}

void RoundEdit::selectAll()
{
    m_cursor.select(QTextCursor::Document);
    cursorMoved();
}

void RoundEdit::deleteSelection()
{
    if (m_cursor.hasSelection()) {
        m_cursor.removeSelectedText();
        cursorMoved();
    }
}

// ---------------------------------------------------------------------------
// Pages and geometry

int RoundEdit::pageCount() const
{
    return m_layout->pageCount();
}

void RoundEdit::showPage(int page)
{
    page = qBound(0, page, pageCount() - 1);
    if (page == m_page)
        return;
    m_page = page;
    invalidate();
    Q_EMIT pageChanged(m_page, pageCount());
}

QPointF RoundEdit::center() const
{
    return QPointF(width() / 2.0, height() / 2.0);
}

qreal RoundEdit::hubRadius() const
{
    return qBound(22.0, width() / 2.0 * 0.17, 64.0);
}

int RoundEdit::hubPart(const QPointF &pos) const
{
    const QPointF v = pos - center();
    if (std::hypot(v.x(), v.y()) > hubRadius() - 2)
        return -1;
    return v.y() < 0 ? 0 : 1;
}

void RoundEdit::setColors(const Colors &colors)
{
    m_colors = colors;
    invalidate();
}

void RoundEdit::setOuterMargin(qreal margin)
{
    m_outerMargin = margin;
    relayoutDisc();
}

void RoundEdit::setTabStopDistance(qreal distance)
{
    QTextOption option = m_doc->defaultTextOption();
    option.setTabStopDistance(distance);
    m_doc->setDefaultTextOption(option);
}

void RoundEdit::relayoutDisc()
{
    const qreal outer = width() / 2.0 - 4 - m_outerMargin;
    m_layout->setDisc(qMax(outer, hubRadius() + 30), hubRadius() + 4);
    cursorMoved(true);
    invalidate();
}

void RoundEdit::resizeEvent(QResizeEvent *)
{
    setMask(QRegion(rect(), QRegion::Ellipse));
    relayoutDisc();
}

void RoundEdit::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::FontChange) {
        m_doc->setDefaultFont(font());
        cursorMoved(true);
        invalidate();
    }
    QWidget::changeEvent(event);
}

// ---------------------------------------------------------------------------
// Cursor

void RoundEdit::cursorMoved(bool keepGoal)
{
    if (!keepGoal)
        m_hasGoal = false;

    const int page = m_layout->pageOfPosition(m_cursor.position());
    if (page != m_page) {
        m_page = qBound(0, page, pageCount() - 1);
        invalidate();
        Q_EMIT pageChanged(m_page, pageCount());
    }

    if (m_cursor.position() != m_lastPosition || m_cursor.anchor() != m_lastAnchor) {
        const bool hadSelection = m_lastPosition != m_lastAnchor;
        const bool selection = hadSelection || m_cursor.hasSelection();
        m_lastPosition = m_cursor.position();
        m_lastAnchor = m_cursor.anchor();
        Q_EMIT cursorPositionChanged();
        if (selection) {
            invalidate();
            Q_EMIT selectionChanged();
        }
    }

    m_cursorOn = true;
    if (hasFocus())
        m_blink.start(QApplication::cursorFlashTime() / 2, this);
    update();
}

void RoundEdit::moveByRings(int delta, QTextCursor::MoveMode mode)
{
    const int ring = m_layout->ringOfPosition(m_cursor.position());
    if (!m_hasGoal) {
        m_goalAngle = m_layout->angleOfPosition(m_cursor.position());
        m_hasGoal = true;
    }
    const int target = ring + delta;
    int position;
    if (target < 0)
        position = 0;
    else if (target >= m_layout->ringCount())
        position = m_doc->characterCount() - 1;
    else
        position = m_layout->positionOnRing(target, m_goalAngle);
    m_cursor.setPosition(position, mode);
    cursorMoved(true);
}

void RoundEdit::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_blink.timerId()) {
        m_cursorOn = !m_cursorOn;
        update();
        return;
    }
    QWidget::timerEvent(event);
}

void RoundEdit::focusInEvent(QFocusEvent *event)
{
    m_cursorOn = true;
    m_blink.start(QApplication::cursorFlashTime() / 2, this);
    update();
    QWidget::focusInEvent(event);
}

void RoundEdit::focusOutEvent(QFocusEvent *event)
{
    m_blink.stop();
    m_cursorOn = false;
    update();
    QWidget::focusOutEvent(event);
}

// ---------------------------------------------------------------------------
// Painting

void RoundEdit::invalidate()
{
    m_cacheValid = false;
    update();
}

void RoundEdit::rebuildCache()
{
    const qreal dpr = devicePixelRatioF();
    if (m_cache.size() != size() * dpr)
        m_cache = QPixmap(size() * dpr);
    m_cache.setDevicePixelRatio(dpr);
    m_cache.fill(Qt::transparent);

    QPainter p(&m_cache);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.translate(center());
    m_layout->paintGuides(&p, m_colors.guide);
    m_layout->paintPage(&p, m_page, m_colors.ink, m_colors.selection, m_cursor.selectionStart(),
                        m_cursor.selectionEnd());
    m_cacheValid = true;
}

void RoundEdit::paintHub(QPainter &p)
{
    const QPointF c = center();
    const qreal r = hubRadius() - 2;
    p.setPen(QPen(m_colors.hubEdge, 1.2));
    p.setBrush(m_colors.hub);
    p.drawEllipse(c, r, r);

    const int count = pageCount();
    if (count <= 1) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_colors.hubEdge);
        p.drawEllipse(c, r * 0.12, r * 0.12);
        return;
    }

    // Each half of the hub is a button: the upper turns back, the lower on.
    for (int part = 0; part < 2; ++part) {
        const bool enabled = part == 0 ? m_page > 0 : m_page < count - 1;
        if (enabled && m_hubHover == part) {
            QPainterPath half;
            half.moveTo(c);
            half.arcTo(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), part == 0 ? 0 : 180, 180);
            half.closeSubpath();
            p.fillPath(half, m_colors.hubHover);
        }
        QColor chevron = m_colors.hubInk;
        if (!enabled)
            chevron.setAlpha(60);
        const qreal s = r * 0.2;
        const qreal y = part == 0 ? -r * 0.55 : r * 0.55;
        const qreal tip = part == 0 ? -s * 0.55 : s * 0.55;
        p.setPen(QPen(chevron, qMax(1.3, r * 0.06), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(QPolygonF({ c + QPointF(-s, y - tip), c + QPointF(0, y + tip), c + QPointF(s, y - tip) }));
    }

    QFont font(QStringLiteral("Segoe UI Variable Text"));
    font.setPixelSize(qMax(9, qRound(r * 0.3)));
    font.setWeight(QFont::DemiBold);
    p.setFont(font);
    p.setPen(m_colors.hubInk);
    p.drawText(QRectF(c.x() - r, c.y() - r / 2, 2 * r, r), Qt::AlignCenter,
               QStringLiteral("%1/%2").arg(m_page + 1).arg(count));
}

void RoundEdit::paintEvent(QPaintEvent *)
{
    if (!m_cacheValid || m_cache.size() != size() * devicePixelRatioF())
        rebuildCache();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.drawPixmap(0, 0, m_cache);
    paintHub(p);

    if (hasFocus() && m_cursorOn) {
        p.translate(center());
        m_layout->paintCursor(&p, m_page, m_cursor.position(), m_colors.ink, 1.6);
    }
}

// ---------------------------------------------------------------------------
// Keyboard

bool RoundEdit::isEditingKey(const QKeyEvent *event)
{
    for (const MoveBinding &binding : kMoves) {
        if (event->matches(binding.key))
            return true;
    }
    for (const QKeySequence::StandardKey key : kOtherEditingKeys) {
        if (event->matches(key))
            return true;
    }
    switch (event->key()) {
    case Qt::Key_Backspace:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return true;
    case Qt::Key_Tab:
        return event->modifiers() == Qt::NoModifier;
    default:
        break;
    }
    return isTyping(event);
}

bool RoundEdit::event(QEvent *event)
{
    // Claim editing keys before the window's actions see them: Delete with
    // nothing selected must delete a character, not run the menu's Delete.
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (isEditingKey(key)) {
            key->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

void RoundEdit::keyPressEvent(QKeyEvent *event)
{
    for (const MoveBinding &binding : kMoves) {
        if (event->matches(binding.key)) {
            m_cursor.movePosition(binding.operation, binding.mode);
            cursorMoved();
            return;
        }
    }

    const int page = m_layout->ringsPerPage();
    if (event->matches(QKeySequence::MoveToNextLine))
        return moveByRings(1, QTextCursor::MoveAnchor);
    if (event->matches(QKeySequence::MoveToPreviousLine))
        return moveByRings(-1, QTextCursor::MoveAnchor);
    if (event->matches(QKeySequence::SelectNextLine))
        return moveByRings(1, QTextCursor::KeepAnchor);
    if (event->matches(QKeySequence::SelectPreviousLine))
        return moveByRings(-1, QTextCursor::KeepAnchor);
    if (event->matches(QKeySequence::MoveToNextPage))
        return moveByRings(page, QTextCursor::MoveAnchor);
    if (event->matches(QKeySequence::MoveToPreviousPage))
        return moveByRings(-page, QTextCursor::MoveAnchor);
    if (event->matches(QKeySequence::SelectNextPage))
        return moveByRings(page, QTextCursor::KeepAnchor);
    if (event->matches(QKeySequence::SelectPreviousPage))
        return moveByRings(-page, QTextCursor::KeepAnchor);

    if (event->matches(QKeySequence::Delete)) {
        if (m_cursor.hasSelection())
            m_cursor.removeSelectedText();
        else
            m_cursor.deleteChar();
        return cursorMoved();
    }
    if (event->matches(QKeySequence::DeleteEndOfWord)) {
        if (!m_cursor.hasSelection())
            m_cursor.movePosition(QTextCursor::NextWord, QTextCursor::KeepAnchor);
        m_cursor.removeSelectedText();
        return cursorMoved();
    }
    if (event->matches(QKeySequence::DeleteStartOfWord)) {
        if (!m_cursor.hasSelection())
            m_cursor.movePosition(QTextCursor::PreviousWord, QTextCursor::KeepAnchor);
        m_cursor.removeSelectedText();
        return cursorMoved();
    }
    if (event->matches(QKeySequence::Undo))
        return undo();
    if (event->matches(QKeySequence::Redo))
        return redo();
    if (event->matches(QKeySequence::Copy))
        return copy();
    if (event->matches(QKeySequence::Cut))
        return cut();
    if (event->matches(QKeySequence::Paste))
        return paste();
    if (event->matches(QKeySequence::SelectAll))
        return selectAll();

    switch (event->key()) {
    case Qt::Key_Backspace:
        if (!(event->modifiers() & ~Qt::ShiftModifier)) {
            m_cursor.deletePreviousChar();
            return cursorMoved();
        }
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        m_cursor.insertBlock();
        return cursorMoved();
    case Qt::Key_Tab:
        if (event->modifiers() == Qt::NoModifier) {
            m_cursor.insertText(QStringLiteral("\t"));
            return cursorMoved();
        }
        break;
    default:
        break;
    }

    if (isTyping(event)) {
        m_cursor.insertText(event->text());
        return cursorMoved();
    }
    event->ignore();
}

void RoundEdit::inputMethodEvent(QInputMethodEvent *event)
{
    if (!event->commitString().isEmpty()) {
        m_cursor.insertText(event->commitString());
        cursorMoved();
    }
    event->accept();
}

QVariant RoundEdit::inputMethodQuery(Qt::InputMethodQuery query) const
{
    switch (query) {
    case Qt::ImEnabled:
        return true;
    case Qt::ImCursorRectangle: {
        qreal x = 0;
        const int ring = m_layout->ringOfPosition(m_cursor.position(), &x);
        const RingTextLayout::Ring &g = m_layout->ring(ring);
        const QPointF at = Arc::polar(center(), g.baseline, g.start + x / g.mid);
        return QRectF(at.x() - 1, at.y() - m_layout->ascent(), 2, m_layout->ascent() + m_layout->descent());
    }
    case Qt::ImCursorPosition:
        return m_cursor.positionInBlock();
    case Qt::ImAnchorPosition:
        return m_cursor.anchor() - m_cursor.block().position();
    case Qt::ImSurroundingText:
        return m_cursor.block().text();
    case Qt::ImCurrentSelection:
        return m_cursor.selectedText();
    default:
        return QWidget::inputMethodQuery(query);
    }
}

// ---------------------------------------------------------------------------
// Mouse

void RoundEdit::mousePressEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    const int part = hubPart(pos);
    if (part >= 0) {
        if (event->button() == Qt::LeftButton)
            showPage(m_page + (part == 0 ? -1 : 1));
        return;
    }

    setFocus(Qt::MouseFocusReason);
    const int position = m_layout->positionAt(m_page, pos - center());

    if (event->button() == Qt::RightButton) {
        // Right-clicking outside the selection moves the cursor there first.
        if (position < m_cursor.selectionStart() || position > m_cursor.selectionEnd()) {
            m_cursor.setPosition(position);
            cursorMoved();
        }
        Q_EMIT contextMenuRequested(Arc::angleOf(center(), pos));
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;

    // A press soon after a double-click is a triple-click: select the line.
    if (m_doubleClick.isValid() && m_doubleClick.elapsed() < QGuiApplication::styleHints()->mouseDoubleClickInterval()) {
        m_doubleClick.invalidate();
        m_cursor.setPosition(position);
        m_cursor.movePosition(QTextCursor::StartOfBlock);
        m_cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
        if (m_cursor.position() == m_cursor.anchor())
            m_cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursorMoved();
        return;
    }

    m_cursor.setPosition(position, event->modifiers() & Qt::ShiftModifier ? QTextCursor::KeepAnchor
                                                                         : QTextCursor::MoveAnchor);
    m_selecting = true;
    cursorMoved();
}

void RoundEdit::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    if (m_selecting && (event->buttons() & Qt::LeftButton)) {
        m_cursor.setPosition(m_layout->positionAt(m_page, pos - center()), QTextCursor::KeepAnchor);
        cursorMoved();
        return;
    }

    const int part = hubPart(pos);
    const bool enabled = part == 0 ? m_page > 0 : (part == 1 && m_page < pageCount() - 1);
    const int hover = enabled ? part : -1;
    if (hover != m_hubHover) {
        m_hubHover = hover;
        update();
    }
    setCursor(part >= 0 ? (enabled ? Qt::PointingHandCursor : Qt::ArrowCursor) : Qt::IBeamCursor);
}

void RoundEdit::mouseReleaseEvent(QMouseEvent *)
{
    m_selecting = false;
}

void RoundEdit::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (hubPart(event->position()) >= 0) {
        mousePressEvent(event);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    m_cursor.setPosition(m_layout->positionAt(m_page, event->position() - center()));
    m_cursor.select(QTextCursor::WordUnderCursor);
    m_selecting = false;
    m_doubleClick.start();
    cursorMoved();
}

void RoundEdit::leaveEvent(QEvent *)
{
    if (m_hubHover != -1) {
        m_hubHover = -1;
        update();
    }
}

void RoundEdit::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (event->modifiers() & Qt::ControlModifier) {
        m_zoomAccum += delta;
        const int steps = m_zoomAccum / 120;
        if (steps != 0) {
            m_zoomAccum -= steps * 120;
            Q_EMIT zoomRequested(steps);
        }
        event->accept();
        return;
    }

    if ((delta > 0) != (m_wheelAccum > 0))
        m_wheelAccum = 0;
    m_wheelAccum += delta;
    while (m_wheelAccum >= 120) {
        m_wheelAccum -= 120;
        showPage(m_page - 1);
    }
    while (m_wheelAccum <= -120) {
        m_wheelAccum += 120;
        showPage(m_page + 1);
    }
    event->accept();
}

// ---------------------------------------------------------------------------
// Drag and drop

void RoundEdit::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText())
        event->acceptProposedAction();
}

void RoundEdit::dragMoveEvent(QDragMoveEvent *event)
{
    event->acceptProposedAction();
}

void RoundEdit::dropEvent(QDropEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (mime->hasUrls()) {
        QStringList paths;
        for (const QUrl &url : mime->urls()) {
            if (url.isLocalFile())
                paths.append(url.toLocalFile());
        }
        if (!paths.isEmpty()) {
            event->acceptProposedAction();
            Q_EMIT filesDropped(paths);
            return;
        }
    }
    if (mime->hasText()) {
        event->acceptProposedAction();
        m_cursor.setPosition(m_layout->positionAt(m_page, event->position() - center()));
        QString text = mime->text();
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        m_cursor.insertText(text);
        cursorMoved();
    }
}
