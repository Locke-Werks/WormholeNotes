#include "NoteWindow.h"

#include "Arc.h"
#include "RadialMenu.h"
#include "RadialPrompt.h"
#include "RoundEdit.h"

#include <QAction>
#include <QCloseEvent>
#include <QActionGroup>
#include <QApplication>
#include <QDateTime>
#include <QFontDatabase>
#include <QFontDialog>
#include <QGuiApplication>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QStyleHints>
#include <QTextBlock>
#include <QTextDocument>
#include <QWindow>

#include <cmath>

namespace {

constexpr int kShadow = 16;
constexpr int kEdgeGrip = 7;
constexpr int kMinRadius = 140;
constexpr int kDefaultRadius = 220;
constexpr int kMinZoom = 10;
constexpr int kMaxZoom = 500;

// Degrees around the bezel, screen convention: 0 is three o'clock, -90 noon.
// The menu bar starts just above nine o'clock and runs clockwise toward noon;
// the title fills the arc between it and the window buttons.
constexpr qreal kMenuBarStart = -172;
constexpr qreal kButtonAngle[] = { -27, -14 };
constexpr qreal kStatusSpan = 120;

const QColor kAccent(0xd6, 0x26, 0x2a);

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

} // namespace

NoteWindow::NoteWindow(QWidget *parent)
    : QWidget(parent)
{
    // A tool window, so the note has no taskbar button of its own: it belongs
    // to whatever window its hole is on.
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);

    m_edit = new RoundEdit(this);
    m_prompt = new RadialPrompt(this);
    m_radial = new RadialMenu(this);
    setFocusProxy(m_edit);

    m_flashTimer.setSingleShot(true);
    connect(&m_flashTimer, &QTimer::timeout, this, [this] {
        m_flash.clear();
        update();
    });

    createActions();

    QTextDocument *doc = m_edit->document();
    connect(doc, &QTextDocument::contentsChanged, this, [this] {
        if (!m_loading)
            emit textEdited(text());
    });
    connect(doc, &QTextDocument::undoAvailable, this, &NoteWindow::updateActions);
    connect(doc, &QTextDocument::redoAvailable, this, &NoteWindow::updateActions);
    connect(doc, &QTextDocument::contentsChanged, this, &NoteWindow::updateActions);
    connect(m_edit, &RoundEdit::selectionChanged, this, &NoteWindow::updateActions);
    connect(m_edit, &RoundEdit::cursorPositionChanged, this, qOverload<>(&QWidget::update));
    connect(m_edit, &RoundEdit::zoomRequested, this, &NoteWindow::zoomBy);
    connect(m_edit, &RoundEdit::contextMenuRequested, this, [this](qreal angle) {
        m_radial->open(m_contextMenu, angle);
    });
    connect(m_radial, &RadialMenu::closed, this, [this] {
        m_edit->setFocus();
        update();
    });
    connect(m_prompt, &RadialPrompt::buttonClicked, this, &NoteWindow::onPromptButton);
    connect(m_prompt, &RadialPrompt::dismissed, this, [this] {
        m_promptMode = PromptMode::None;
        m_edit->setOuterMargin(0);
        m_edit->setFocus();
    });
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        applyTheme();
        update();
    });

    loadSettings();
    applyTheme();
    applyFont();
    updateActions();
}

NoteWindow::~NoteWindow()
{
    saveSettings();
}

void NoteWindow::setSheet(const QString &label, const QString &text)
{
    m_loading = true;
    m_edit->setPlainText(text);
    m_edit->document()->clearUndoRedoStacks();
    m_edit->moveCursor(QTextCursor::End);
    m_loading = false;
    setWindowTitle(label);
    updateActions();
    update();
}

QString NoteWindow::text() const
{
    return m_edit->exactText();
}

void NoteWindow::openAt(const QPoint &globalCenter)
{
    m_maximized = false;
    const QScreen *s = QGuiApplication::screenAt(globalCenter);
    if (!s)
        s = QGuiApplication::primaryScreen();
    const QRect area = s->availableGeometry();
    m_radius = qBound(kMinRadius, m_restoreRadius, maximumRadius());
    const int reach = m_radius + kShadow;
    // Out of the hole, but never off the screen.
    const QPoint c(qBound(area.left() + reach, globalCenter.x(), qMax(area.left() + reach, area.right() - reach)),
                   qBound(area.top() + reach, globalCenter.y(), qMax(area.top() + reach, area.bottom() - reach)));
    setCircle(c, m_radius);
    show();
    raise();
    activateWindow();
    m_edit->setFocus();
}

void NoteWindow::putAway()
{
    if (!isVisible())
        return;
    m_radial->close();
    m_prompt->dismiss();
    if (!m_maximized)
        m_restoreRadius = m_radius;
    saveSettings();
    hide();
}

// Switching to another app puts the note away, the way a sticky note folds
// back when you look at something else. Checked from the event loop, since
// focus passes through nothing on its way to one of our own dialogs.
void NoteWindow::checkStillActive()
{
    if (!isVisible() || m_choosingFont || modalOpen())
        return;
    if (!QApplication::activeWindow())
        emit putAwayRequested();
}

// ---------------------------------------------------------------------------
// Geometry

QPointF NoteWindow::center() const
{
    return QPointF(width() / 2.0, height() / 2.0);
}

int NoteWindow::ringWidth() const
{
    return qBound(28, qRound(m_radius * 0.115), 44);
}

qreal NoteWindow::innerRadius() const
{
    return m_radius - ringWidth();
}

qreal NoteWindow::ringMid() const
{
    return m_radius - ringWidth() / 2.0;
}

qreal NoteWindow::buttonRadius() const
{
    return ringWidth() * 0.34;
}

QPointF NoteWindow::buttonCenter(Button button) const
{
    return Arc::polar(center(), ringMid(), Arc::radians(kButtonAngle[button]));
}

QFont NoteWindow::bezelFont(qreal scale, bool bold) const
{
    QFont font(QStringLiteral("Segoe UI Variable Text"));
    font.setPixelSize(qMax(9, qRound(ringWidth() * scale)));
    if (bold)
        font.setWeight(QFont::DemiBold);
    return font;
}

QList<NoteWindow::Header> NoteWindow::headers() const
{
    QList<Header> out;
    const QFontMetricsF metrics(bezelFont(0.38, true));
    const qreal mid = ringMid();
    qreal angle = Arc::radians(kMenuBarStart);
    for (QMenu *menu : m_menus) {
        Header header;
        header.menu = menu;
        header.label = Arc::stripMnemonic(menu->title());
        header.from = angle;
        header.sweep = (Arc::advance(metrics, header.label) + 18) / mid;
        angle += header.sweep + 2 / mid;
        out.append(header);
    }
    return out;
}

NoteWindow::Zone NoteWindow::zoneAt(const QPointF &pos, int *index) const
{
    const qreal d = QLineF(center(), pos).length();
    if (d > m_radius)
        return Zone::Outside;
    if (d < innerRadius())
        return Zone::Face;
    for (int b = 0; b < ButtonCount; ++b) {
        if (QLineF(buttonCenter(Button(b)), pos).length() <= buttonRadius() + 2) {
            if (index)
                *index = b;
            return Zone::Button;
        }
    }
    if (d >= m_radius - kEdgeGrip)
        return Zone::Edge;
    const qreal angle = Arc::angleOf(center(), pos);
    const QList<Header> list = headers();
    for (int h = 0; h < list.size(); ++h) {
        if (Arc::within(angle, list.at(h).from, list.at(h).sweep)) {
            if (index)
                *index = h;
            return Zone::Header;
        }
    }
    return Zone::Ring;
}

int NoteWindow::maximumRadius() const
{
    const QScreen *s = screen() ? screen() : QGuiApplication::primaryScreen();
    const QRect area = s->availableGeometry();
    return qMax(kMinRadius, qMin(area.width(), area.height()) / 2 - kShadow);
}

void NoteWindow::setCircle(const QPoint &globalCenter, int radius)
{
    m_radius = qBound(kMinRadius, radius, maximumRadius());
    const int side = 2 * (m_radius + kShadow);
    setGeometry(globalCenter.x() - side / 2, globalCenter.y() - side / 2, side, side);
    update();
}

void NoteWindow::resizeEvent(QResizeEvent *)
{
    // The OS can resize us too (a DPI change), so the radius follows the size.
    m_radius = width() / 2 - kShadow;
    const qreal inner = innerRadius();
    const int side = 2 * int(std::floor(inner));
    const QPointF c = center();
    m_edit->setGeometry(qRound(c.x() - side / 2.0), qRound(c.y() - side / 2.0), side, side);
    m_radial->setGeometry(rect());
    m_radial->setDisc(c, inner);
    m_prompt->setGeometry(rect());
    m_prompt->setDisc(c, inner);
    if (m_promptMode != PromptMode::None)
        m_edit->setOuterMargin(m_prompt->depth());
}

// ---------------------------------------------------------------------------
// Painting

NoteWindow::Theme NoteWindow::theme() const
{
    Theme t;
    t.dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    if (t.dark) {
        t.face = QColor(0x1f, 0x1d, 0x1c);
        t.faceEdge = QColor(0x16, 0x15, 0x14);
        t.ink = QColor(0xec, 0xe7, 0xde);
        t.ringTop = QColor(0xb8, 0x25, 0x2a);
        t.ringBottom = QColor(0x6c, 0x13, 0x17);
        t.ringInk = QColor(0xfd, 0xee, 0xea);
        t.ringDim = QColor(0xfd, 0xee, 0xea, 165);
        t.selection = QColor(0xd6, 0x26, 0x2a, 120);
        t.band = QColor(0x2b, 0x28, 0x26, 246);
        t.bandEdge = QColor(255, 255, 255, 34);
        t.field = QColor(0x14, 0x13, 0x12);
    } else {
        t.face = QColor(0xfb, 0xf8, 0xf1);
        t.faceEdge = QColor(0xec, 0xe5, 0xd6);
        t.ink = QColor(0x1f, 0x1c, 0x19);
        t.ringTop = QColor(0xe2, 0x36, 0x3b);
        t.ringBottom = QColor(0xa4, 0x1b, 0x20);
        t.ringInk = QColor(0xff, 0xf7, 0xf4);
        t.ringDim = QColor(0xff, 0xf7, 0xf4, 180);
        t.selection = QColor(0xd6, 0x26, 0x2a, 70);
        t.band = QColor(0xff, 0xfd, 0xf8, 248);
        t.bandEdge = QColor(0, 0, 0, 38);
        t.field = QColor(0xff, 0xff, 0xff);
    }
    if (!isActiveWindow()) {
        t.ringTop = t.ringTop.darker(125);
        t.ringBottom = t.ringBottom.darker(125);
    }
    return t;
}

void NoteWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    const Theme t = theme();
    const QPointF c = center();
    const qreal r = m_radius;
    const qreal inner = innerRadius();

    // Soft drop shadow, since a frameless window gets none from DWM.
    {
        const QPointF sc = c + QPointF(0, 3);
        const qreal sr = r + kShadow - 3;
        const qreal edge = (r - 4) / sr;
        QRadialGradient g(sc, sr);
        g.setColorAt(0, QColor(0, 0, 0, 80));
        g.setColorAt(edge, QColor(0, 0, 0, 80));
        g.setColorAt((edge + 1) / 2, QColor(0, 0, 0, 22));
        g.setColorAt(1, QColor(0, 0, 0, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(sc, sr, sr);
    }

    // Bezel.
    QLinearGradient bezel(c.x(), c.y() - r, c.x(), c.y() + r);
    bezel.setColorAt(0, t.ringTop);
    bezel.setColorAt(1, t.ringBottom);
    p.setBrush(bezel);
    p.drawEllipse(c, r, r);

    QLinearGradient shine(c.x(), c.y() - r, c.x(), c.y() + r);
    shine.setColorAt(0, QColor(255, 255, 255, 110));
    shine.setColorAt(0.5, QColor(255, 255, 255, 0));
    shine.setColorAt(1, QColor(0, 0, 0, 60));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QBrush(shine), 1.4));
    p.drawEllipse(c, r - 0.9, r - 0.9);

    // Face.
    QRadialGradient face(c, inner);
    face.setColorAt(0, t.face);
    face.setColorAt(0.86, t.face);
    face.setColorAt(1, t.faceEdge);
    p.setPen(Qt::NoPen);
    p.setBrush(face);
    p.drawEllipse(c, inner, inner);

    QLinearGradient lip(c.x(), c.y() - inner, c.x(), c.y() + inner);
    lip.setColorAt(0, QColor(0, 0, 0, 90));
    lip.setColorAt(1, QColor(255, 255, 255, 60));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QBrush(lip), 1.6));
    p.drawEllipse(c, inner + 0.6, inner + 0.6);

    // The menu bar: each title a segment of the bezel, lit while hovered or open.
    const qreal mid = ringMid();
    const QFont headerFont = bezelFont(0.38, true);
    const QList<Header> list = headers();
    QMenu *open = m_radial->isOpen() ? m_radial->rootMenu() : nullptr;
    for (int h = 0; h < list.size(); ++h) {
        const Header &header = list.at(h);
        const bool lit = header.menu == open || h == m_hoverHeader;
        if (lit) {
            const QPainterPath path = Arc::sector(c, inner + 3, r - 3, header.from, header.sweep);
            p.fillPath(path, QColor(255, 255, 255, header.menu == open ? 70 : 38));
        }
        Arc::drawCentered(p, c, mid, header.from + header.sweep / 2, header.label, headerFont, t.ringInk, 0.3);
    }

    // Title, in the arc between the menu bar and the buttons.
    if (!list.isEmpty()) {
        const qreal from = list.last().from + list.last().sweep + Arc::radians(4);
        const qreal to = Arc::radians(kButtonAngle[MaximizeButton]) - buttonRadius() / mid - Arc::radians(3);
        if (to > from) {
            const QFont titleFont = bezelFont(0.38, false);
            const QString title = Arc::elide(QFontMetricsF(titleFont), windowTitle(), (to - from) * mid);
            Arc::drawCentered(p, c, mid, (from + to) / 2, title, titleFont, t.ringDim, 0.3);
        }
    }

    // Status along the bottom arc, upright.
    if (m_statusVisible) {
        const QFont statusFont = bezelFont(0.34, false);
        const QString status = Arc::elide(QFontMetricsF(statusFont), statusText(),
                                          Arc::radians(kStatusSpan) * mid);
        Arc::drawCentered(p, c, mid, Arc::kSix, status, statusFont, m_flash.isEmpty() ? t.ringDim : t.ringInk,
                          0.3);
    }

    for (int b = 0; b < ButtonCount; ++b)
        drawButton(p, Button(b), t);
}

void NoteWindow::drawButton(QPainter &p, Button button, const Theme &t) const
{
    const QPointF bc = buttonCenter(button);
    const qreal br = buttonRadius();
    const bool hot = m_hoverButton == button;
    const bool down = hot && m_pressed == button;

    QColor glyph = t.ringInk;
    if (hot) {
        QColor fill = QColor(255, 255, 255, down ? 95 : 55);
        if (button == CloseButton) {
            fill = QColor(255, 255, 255, down ? 200 : 235);
            glyph = kAccent;
        }
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawEllipse(bc, br, br);
    }

    p.setPen(QPen(glyph, qMax(1.4, br * 0.14), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const qreal s = br * 0.42;

    // Glyphs are turned to the radius, so they sit square to the bezel.
    p.save();
    p.translate(bc);
    p.rotate(kButtonAngle[button] + 90);
    switch (button) {
    case MaximizeButton:
        // A round window maximizes to a bigger circle, so the glyph is one.
        p.drawEllipse(QPointF(0, 0), m_maximized ? s * 0.5 : s * 0.9, m_maximized ? s * 0.5 : s * 0.9);
        break;
    case CloseButton:
        p.drawLine(QPointF(-s * 0.85, -s * 0.85), QPointF(s * 0.85, s * 0.85));
        p.drawLine(QPointF(-s * 0.85, s * 0.85), QPointF(s * 0.85, -s * 0.85));
        break;
    case ButtonCount:
        break;
    }
    p.restore();
}

// ---------------------------------------------------------------------------
// Mouse and keyboard on the chrome

void NoteWindow::mousePressEvent(QMouseEvent *event)
{
    int index = -1;
    const Zone zone = zoneAt(event->position(), &index);

    if (event->button() == Qt::LeftButton) {
        switch (zone) {
        case Zone::Button:
            m_pressed = index;
            update();
            return;
        case Zone::Header:
            openMenu(index, false);
            return;
        case Zone::Edge:
            m_radial->close();
            m_resizing = true;
            m_maximized = false;
            m_resizeCenter = mapToGlobal(center());
            m_resizeOffset = m_radius - QLineF(m_resizeCenter, event->globalPosition()).length();
            return;
        case Zone::Ring:
            m_radial->close();
            m_maximized = false;
            if (windowHandle())
                windowHandle()->startSystemMove();
            return;
        case Zone::Face:
        case Zone::Outside:
            break;
        }
    } else if (event->button() == Qt::RightButton && zone != Zone::Face && zone != Zone::Outside) {
        if (modalOpen())
            return;
        m_radial->open(m_rootMenu, Arc::angleOf(center(), event->position()));
        update();
        return;
    }
    QWidget::mousePressEvent(event);
}

void NoteWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_resizing) {
        const qreal d = QLineF(m_resizeCenter, event->globalPosition()).length();
        setCircle(m_resizeCenter.toPoint(), qRound(d + m_resizeOffset));
        return;
    }

    int index = -1;
    const Zone zone = zoneAt(event->position(), &index);
    const int button = zone == Zone::Button ? index : -1;
    const int header = zone == Zone::Header ? index : -1;
    if (button != m_hoverButton || header != m_hoverHeader) {
        m_hoverButton = button;
        m_hoverHeader = header;
        update();
    }

    if (zone == Zone::Edge) {
        const QPointF v = event->position() - center();
        const qreal a = std::fmod(Arc::degrees(std::atan2(v.y(), v.x())) + 360.0, 180.0);
        if (a < 22.5 || a >= 157.5)
            setCursor(Qt::SizeHorCursor);
        else if (a < 67.5)
            setCursor(Qt::SizeFDiagCursor);
        else if (a < 112.5)
            setCursor(Qt::SizeVerCursor);
        else
            setCursor(Qt::SizeBDiagCursor);
    } else if (zone == Zone::Button || zone == Zone::Header) {
        setCursor(Qt::PointingHandCursor);
    } else {
        unsetCursor();
    }
}

void NoteWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_resizing) {
        m_resizing = false;
        return;
    }
    if (m_pressed >= 0) {
        const int pressed = m_pressed;
        m_pressed = -1;
        update();
        int index = -1;
        if (zoneAt(event->position(), &index) == Zone::Button && index == pressed)
            triggerButton(Button(index));
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void NoteWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && zoneAt(event->position()) == Zone::Ring) {
        toggleMaximize();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void NoteWindow::leaveEvent(QEvent *)
{
    if (m_hoverButton != -1 || m_hoverHeader != -1) {
        m_hoverButton = -1;
        m_hoverHeader = -1;
        update();
    }
}

void NoteWindow::keyPressEvent(QKeyEvent *event)
{
    // Escape in the text closes a Find or Replace ring left open beside it.
    if (event->key() == Qt::Key_Escape) {
        if (m_prompt->isOpen())
            m_prompt->dismiss();
        else if (m_radial->isOpen())
            m_radial->close();
        else
            emit putAwayRequested();
        return;
    }
    QWidget::keyPressEvent(event);
}

void NoteWindow::openMenu(int header, bool fromKeyboard)
{
    const QList<Header> list = headers();
    if (header < 0 || header >= list.size() || modalOpen())
        return;
    const Header &h = list.at(header);
    if (!fromKeyboard && m_radial->isOpen() && m_radial->rootMenu() == h.menu) {
        m_radial->close();
        return;
    }
    m_radial->open(h.menu, h.from + h.sweep / 2, fromKeyboard);
    update();
}

void NoteWindow::triggerButton(Button button)
{
    switch (button) {
    case MaximizeButton: toggleMaximize(); break;
    case CloseButton: emit putAwayRequested(); break;
    case ButtonCount: break;
    }
}

void NoteWindow::toggleMaximize()
{
    if (m_maximized) {
        m_maximized = false;
        setCircle(m_restoreCenter, m_restoreRadius);
    } else {
        m_restoreCenter = mapToGlobal(center()).toPoint();
        m_restoreRadius = m_radius;
        m_maximized = true;
        const QScreen *s = screen() ? screen() : QGuiApplication::primaryScreen();
        setCircle(s->availableGeometry().center(), maximumRadius());
    }
}

// ---------------------------------------------------------------------------
// Window events

void NoteWindow::closeEvent(QCloseEvent *event)
{
    // Alt+F4 puts the note away. Quitting is on the Note menu and the tray.
    event->ignore();
    emit putAwayRequested();
}

bool NoteWindow::modalOpen() const
{
    return m_prompt->isOpen() && m_prompt->isModal();
}

void NoteWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::ActivationChange) {
        update();
        if (!isActiveWindow())
            QTimer::singleShot(0, this, &NoteWindow::checkStillActive);
    }
    QWidget::changeEvent(event);
}

// ---------------------------------------------------------------------------
// Menus and actions

void NoteWindow::createActions()
{
    auto add = [this](QMenu *menu, const QString &text, const QList<QKeySequence> &keys, auto &&slot) {
        QAction *action = menu->addAction(text);
        action->setShortcuts(keys);
        connect(action, &QAction::triggered, this, slot);
        // On the window too, so the shortcut works with no menu open. The
        // menus are never shown as QMenus; RadialMenu draws them.
        addAction(action);
        return action;
    };

    QMenu *note = new QMenu(tr("&Note"), this);
    add(note, tr("&Put Away"), {}, [this] { emit putAwayRequested(); });
    note->addSeparator();
    add(note, tr("&Quit WormholeNotes"), { QKeySequence(tr("Ctrl+Q")) }, [this] { emit quitRequested(); });

    QMenu *edit = new QMenu(tr("&Edit"), this);
    m_undo = add(edit, tr("&Undo"), { QKeySequence::Undo }, [this] { m_edit->undo(); });
    m_redo = add(edit, tr("&Redo"), { QKeySequence(tr("Ctrl+Y")) }, [this] { m_edit->redo(); });
    edit->addSeparator();
    m_cut = add(edit, tr("Cu&t"), { QKeySequence::Cut }, [this] { m_edit->cut(); });
    m_copy = add(edit, tr("&Copy"), { QKeySequence::Copy }, [this] { m_edit->copy(); });
    QAction *paste = add(edit, tr("&Paste"), { QKeySequence::Paste }, [this] { m_edit->paste(); });
    m_delete = add(edit, tr("De&lete"), { QKeySequence::Delete }, [this] { m_edit->deleteSelection(); });
    edit->addSeparator();
    add(edit, tr("&Find..."), { QKeySequence::Find }, [this] { showFind(false); });
    m_findNextAction = add(edit, tr("Find &Next"), { QKeySequence(Qt::Key_F3) }, [this] { findNext(false); });
    m_findPrevAction = add(edit, tr("Find Pre&vious"), { QKeySequence(Qt::SHIFT | Qt::Key_F3) },
                           [this] { findNext(true); });
    add(edit, tr("R&eplace..."), { QKeySequence(tr("Ctrl+H")) }, [this] { showFind(true); });
    add(edit, tr("&Go To..."), { QKeySequence(tr("Ctrl+G")) }, [this] { goToLine(); });
    edit->addSeparator();
    QAction *selectAll = add(edit, tr("Select &All"), { QKeySequence::SelectAll }, [this] { m_edit->selectAll(); });
    add(edit, tr("Time/&Date"), { QKeySequence(Qt::Key_F5) }, [this] { insertTimeDate(); });

    QMenu *format = new QMenu(tr("F&ormat"), this);
    m_familyMenu = format->addMenu(tr("&Font"));
    m_sizeMenu = format->addMenu(tr("&Size"));
    m_familyGroup = new QActionGroup(this);
    m_sizeGroup = new QActionGroup(this);
    connect(m_familyMenu, &QMenu::aboutToShow, this, &NoteWindow::rebuildFamilyMenu);
    connect(m_sizeMenu, &QMenu::aboutToShow, this, &NoteWindow::rebuildSizeMenu);
    m_boldAction = add(format, tr("&Bold"), {}, [this](bool on) {
        m_baseFont.setBold(on);
        applyFont();
    });
    m_boldAction->setCheckable(true);
    m_italicAction = add(format, tr("&Italic"), {}, [this](bool on) {
        m_baseFont.setItalic(on);
        applyFont();
    });
    m_italicAction->setCheckable(true);
    add(format, tr("&More Fonts..."), {}, [this] { chooseFont(); });

    QMenu *view = new QMenu(tr("&View"), this);
    QMenu *zoom = view->addMenu(tr("&Zoom"));
    add(zoom, tr("Zoom &In"), { QKeySequence::ZoomIn, QKeySequence(tr("Ctrl+=")) }, [this] { zoomBy(1); });
    add(zoom, tr("Zoom &Out"), { QKeySequence::ZoomOut }, [this] { zoomBy(-1); });
    add(zoom, tr("&Restore"), { QKeySequence(tr("Ctrl+0")) }, [this] { setZoom(100); });
    m_statusAction = add(view, tr("&Status Bar"), {}, [this](bool on) {
        m_statusVisible = on;
        update();
    });
    m_statusAction->setCheckable(true);
    view->addSeparator();
    add(view, tr("Pre&vious Page"), { QKeySequence(Qt::CTRL | Qt::Key_PageUp) },
        [this] { m_edit->showPage(m_edit->currentPage() - 1); });
    add(view, tr("Ne&xt Page"), { QKeySequence(Qt::CTRL | Qt::Key_PageDown) },
        [this] { m_edit->showPage(m_edit->currentPage() + 1); });

    QMenu *help = new QMenu(tr("&Help"), this);
    add(help, tr("&About WormholeNotes"), {}, [this] { about(); });

    m_menus = { note, edit, format, view, help };

    m_rootMenu = new QMenu(this);
    for (QMenu *menu : m_menus)
        m_rootMenu->addMenu(menu);

    m_contextMenu = new QMenu(this);
    m_contextMenu->addAction(m_undo);
    m_contextMenu->addAction(m_redo);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_cut);
    m_contextMenu->addAction(m_copy);
    m_contextMenu->addAction(paste);
    m_contextMenu->addAction(m_delete);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(selectAll);

    // Alt+letter opens that menu from its place on the bezel; F10 opens Note.
    const Qt::Key keys[] = { Qt::Key_N, Qt::Key_E, Qt::Key_O, Qt::Key_V, Qt::Key_H };
    for (int i = 0; i < 5; ++i) {
        auto *shortcut = new QShortcut(QKeySequence(Qt::ALT | keys[i]), this);
        connect(shortcut, &QShortcut::activated, this, [this, i] { openMenu(i, true); });
    }
    auto *menuKey = new QShortcut(QKeySequence(Qt::Key_F10), this);
    connect(menuKey, &QShortcut::activated, this, [this] { openMenu(0, true); });
}

// Both menus are refilled each time they open, so they always show the font
// in use. clear() deletes the old actions, and a deleted action leaves its
// group on its own, so the groups live as long as the window.
void NoteWindow::rebuildFamilyMenu()
{
    // Monospaced faces first, as Tondo had them, then a few others worth
    // reading at length. Only the ones actually installed appear.
    static const QStringList preferred = {
        QStringLiteral("Consolas"), QStringLiteral("Cascadia Mono"), QStringLiteral("Cascadia Code"),
        QStringLiteral("Lucida Console"), QStringLiteral("Courier New"), QStringLiteral("Segoe UI"),
        QStringLiteral("Georgia"), QStringLiteral("Palatino Linotype"), QStringLiteral("Comic Sans MS"),
    };
    const QStringList installed = QFontDatabase::families();
    QStringList families;
    for (const QString &family : preferred) {
        if (installed.contains(family))
            families.append(family);
    }
    if (!families.contains(m_baseFont.family()))
        families.prepend(m_baseFont.family());

    m_familyMenu->clear();
    for (const QString &family : families) {
        QAction *action = m_familyMenu->addAction(family);
        action->setCheckable(true);
        action->setChecked(family == m_baseFont.family());
        m_familyGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, family] {
            m_baseFont.setFamily(family);
            applyFont();
        });
    }
}

void NoteWindow::rebuildSizeMenu()
{
    m_sizeMenu->clear();
    const int current = qRound(m_baseFont.pointSizeF());
    for (const int size : { 8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 36 }) {
        QAction *action = m_sizeMenu->addAction(QString::number(size));
        action->setCheckable(true);
        action->setChecked(size == current);
        m_sizeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, size] {
            m_baseFont.setPointSizeF(size);
            applyFont();
        });
    }
}

void NoteWindow::updateActions()
{
    QTextDocument *doc = m_edit->document();
    const bool selection = m_edit->textCursor().hasSelection();
    m_undo->setEnabled(doc->isUndoAvailable());
    m_redo->setEnabled(doc->isRedoAvailable());
    m_cut->setEnabled(selection);
    m_copy->setEnabled(selection);
    m_delete->setEnabled(selection);
    m_findNextAction->setEnabled(!doc->isEmpty());
    m_findPrevAction->setEnabled(!doc->isEmpty());
    m_statusAction->setChecked(m_statusVisible);
    m_boldAction->setChecked(m_baseFont.bold());
    m_italicAction->setChecked(m_baseFont.italic());
}

QString NoteWindow::statusText() const
{
    if (!m_flash.isEmpty())
        return m_flash;
    const QTextCursor cursor = m_edit->textCursor();
    QString position = tr("Ln %1, Col %2").arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1);
    if (cursor.hasSelection())
        position += tr(" (%1 selected)").arg(cursor.selectionEnd() - cursor.selectionStart());
    const QStringList parts = { position, QStringLiteral("%1%").arg(m_zoom) };
    return parts.join(QStringLiteral("   ·   "));
}

void NoteWindow::flash(const QString &message)
{
    m_flash = message;
    m_flashTimer.start(3500);
    update();
}

void NoteWindow::applyTheme()
{
    const Theme t = theme();

    RoundEdit::Colors edit;
    edit.ink = t.ink;
    edit.selection = t.selection;
    edit.guide = withAlpha(t.ink, t.dark ? 16 : 13);
    edit.hub = t.faceEdge;
    edit.hubEdge = withAlpha(t.ink, 50);
    edit.hubInk = withAlpha(t.ink, 200);
    edit.hubHover = withAlpha(kAccent, 60);
    m_edit->setColors(edit);

    RadialMenu::Colors menu;
    menu.band = t.band;
    menu.bandEdge = t.bandEdge;
    menu.ink = t.ink;
    menu.dim = withAlpha(t.ink, 90);
    menu.accent = kAccent;
    menu.accentInk = QColor(0xff, 0xf7, 0xf4);
    menu.shade = QColor(0, 0, 0, t.dark ? 90 : 36);
    m_radial->setColors(menu);

    RadialPrompt::Colors prompt;
    prompt.band = t.band;
    prompt.bandEdge = t.bandEdge;
    prompt.ink = t.ink;
    prompt.dim = withAlpha(t.ink, 140);
    prompt.accent = kAccent;
    prompt.accentInk = QColor(0xff, 0xf7, 0xf4);
    prompt.shade = QColor(0, 0, 0, t.dark ? 120 : 70);
    prompt.field = t.field;
    prompt.selection = withAlpha(kAccent, 80);
    m_prompt->setColors(prompt);
}

void NoteWindow::applyFont()
{
    QFont font = m_baseFont;
    const qreal base = m_baseFont.pointSizeF() > 0 ? m_baseFont.pointSizeF() : 11.0;
    font.setPointSizeF(qMax(1.0, base * m_zoom / 100.0));
    m_edit->setFont(font);
    m_edit->setTabStopDistance(QFontMetricsF(font).horizontalAdvance(u' ') * 8);
    updateActions();
}

void NoteWindow::zoomBy(int steps)
{
    setZoom(m_zoom + steps * 10);
}

void NoteWindow::setZoom(int percent)
{
    percent = qBound(kMinZoom, percent, kMaxZoom);
    if (percent == m_zoom)
        return;
    m_zoom = percent;
    applyFont();
    update();
}

// ---------------------------------------------------------------------------
// Prompts

int NoteWindow::ask(const QStringList &lines, const QList<QPair<QString, int>> &buttons, int defaultId,
                     int cancelId)
{
    // One question at a time. Resetting a prompt that is still waiting would
    // strand the event loop it is waiting in.
    if (modalOpen())
        return cancelId;
    m_radial->close();
    m_prompt->reset(true);
    for (const QString &line : lines)
        m_prompt->addMessage(line);
    for (const auto &[label, id] : buttons)
        m_prompt->addButton(label, id, id == defaultId);
    m_prompt->setCancelId(cancelId);
    return m_prompt->exec();
}

void NoteWindow::tell(const QStringList &lines)
{
    ask(lines, { { tr("OK"), OkId } }, OkId, OkId);
}

// ---------------------------------------------------------------------------
// Find, replace, go to

void NoteWindow::showFind(bool replaceMode)
{
    if (modalOpen())
        return;
    m_radial->close();
    QString seed = m_edit->textCursor().selectedText();
    // A selection spanning lines is not something anyone meant to search for.
    if (seed.contains(QChar::ParagraphSeparator))
        seed.clear();
    if (seed.isEmpty())
        seed = m_lastSearch.needle;

    m_prompt->reset(false);
    m_promptMode = replaceMode ? PromptMode::Replace : PromptMode::Find;
    m_findField = m_prompt->addField(tr("Find"), seed);
    m_replaceField = replaceMode ? m_prompt->addField(tr("Replace"), m_lastSearch.replacement) : -1;
    m_caseToggle = m_prompt->addToggle(tr("Match case"), m_lastSearch.matchCase);
    m_wrapToggle = m_prompt->addToggle(tr("Wrap around"), m_lastSearch.wrapAround);
    // Notepad's Replace always searches forward, so it has no direction.
    m_upToggle = replaceMode ? -1 : m_prompt->addToggle(tr("Search up"), m_lastSearch.backward);
    m_prompt->addButton(tr("Find Next"), FindNextId, true);
    if (replaceMode) {
        m_prompt->addButton(tr("Replace"), ReplaceId);
        m_prompt->addButton(tr("Replace All"), ReplaceAllId);
    }
    m_prompt->addButton(tr("Close"), CloseId);
    m_prompt->setCancelId(CloseId);
    m_prompt->present();
    // Pull the text in from the rim so the prompt's rings cover none of it.
    m_edit->setOuterMargin(m_prompt->depth());
}

SearchOptions NoteWindow::promptOptions() const
{
    SearchOptions options = m_lastSearch;
    options.needle = m_prompt->fieldText(m_findField);
    if (m_replaceField >= 0)
        options.replacement = m_prompt->fieldText(m_replaceField);
    options.matchCase = m_prompt->toggleState(m_caseToggle);
    options.wrapAround = m_prompt->toggleState(m_wrapToggle);
    options.backward = m_upToggle >= 0 && m_prompt->toggleState(m_upToggle);
    return options;
}

void NoteWindow::onPromptButton(int id)
{
    switch (id) {
    case FindNextId:
        findWith(promptOptions());
        break;
    case ReplaceId:
        replaceOne();
        break;
    case ReplaceAllId:
        replaceAll();
        break;
    case CloseId:
        m_prompt->dismiss();
        break;
    default:
        break;
    }
}

bool NoteWindow::findNext(bool backward)
{
    if (m_lastSearch.needle.isEmpty()) {
        showFind(false);
        return false;
    }
    SearchOptions options = m_promptMode != PromptMode::None ? promptOptions() : m_lastSearch;
    options.backward = backward;
    return findWith(options);
}

bool NoteWindow::findWith(const SearchOptions &options)
{
    m_lastSearch = options;
    if (options.needle.isEmpty())
        return false;

    QTextDocument::FindFlags flags;
    if (options.matchCase)
        flags |= QTextDocument::FindCaseSensitively;
    if (options.backward)
        flags |= QTextDocument::FindBackward;

    QTextDocument *doc = m_edit->document();
    QTextCursor found = doc->find(options.needle, m_edit->textCursor(), flags);
    if (found.isNull() && options.wrapAround) {
        QTextCursor from(doc);
        if (options.backward)
            from.movePosition(QTextCursor::End);
        found = doc->find(options.needle, from, flags);
    }
    if (found.isNull()) {
        flash(tr("Cannot find “%1”").arg(options.needle));
        return false;
    }
    m_edit->setTextCursor(found);
    return true;
}

void NoteWindow::replaceOne()
{
    SearchOptions options = promptOptions();
    options.backward = false;
    QTextCursor cursor = m_edit->textCursor();
    const Qt::CaseSensitivity cs = options.matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (cursor.hasSelection() && QString::compare(cursor.selectedText(), options.needle, cs) == 0) {
        cursor.insertText(options.replacement);
        m_edit->setTextCursor(cursor);
    }
    findWith(options);
}

void NoteWindow::replaceAll()
{
    const SearchOptions options = promptOptions();
    m_lastSearch = options;
    if (options.needle.isEmpty())
        return;

    QTextDocument::FindFlags flags;
    if (options.matchCase)
        flags |= QTextDocument::FindCaseSensitively;

    QTextDocument *doc = m_edit->document();
    QTextCursor batch(doc);
    batch.beginEditBlock();
    int count = 0;
    QTextCursor cursor(doc);
    for (;;) {
        cursor = doc->find(options.needle, cursor, flags);
        if (cursor.isNull())
            break;
        cursor.insertText(options.replacement);
        ++count;
    }
    batch.endEditBlock();
    flash(count == 0 ? tr("Cannot find “%1”").arg(options.needle)
                     : tr("Replaced %n occurrence(s)", nullptr, count));
}

void NoteWindow::goToLine()
{
    if (modalOpen())
        return;
    QTextDocument *doc = m_edit->document();
    m_radial->close();
    m_prompt->reset(true);
    m_prompt->addMessage(tr("Go to line, 1 to %1").arg(doc->blockCount()));
    const int field = m_prompt->addField(tr("Line"), QString::number(m_edit->textCursor().blockNumber() + 1), true);
    m_prompt->addButton(tr("Go To"), OkId, true);
    m_prompt->addButton(tr("Cancel"), CancelId);
    m_prompt->setCancelId(CancelId);
    if (m_prompt->exec() != OkId)
        return;
    const int line = m_prompt->fieldText(field).toInt();
    if (line < 1 || line > doc->blockCount()) {
        flash(tr("The line number is beyond the total number of lines"));
        return;
    }
    m_edit->setTextCursor(QTextCursor(doc->findBlockByNumber(line - 1)));
}

void NoteWindow::insertTimeDate()
{
    const QLocale locale = QLocale::system();
    const QDateTime now = QDateTime::currentDateTime();
    m_edit->insertPlainText(locale.toString(now.time(), QLocale::ShortFormat) + u' '
                            + locale.toString(now.date(), QLocale::ShortFormat));
}

void NoteWindow::chooseFont()
{
    bool ok = false;
    m_choosingFont = true;
    const QFont font = QFontDialog::getFont(&ok, m_baseFont, this, tr("Font"));
    m_choosingFont = false;
    if (ok) {
        m_baseFont = font;
        applyFont();
    }
}

void NoteWindow::about()
{
    tell({ tr("WormholeNotes %1").arg(QCoreApplication::applicationVersion()),
           tr("A sticky note that tunnels through every window."), tr("Built on Tondo by Archon."),
           tr("Locke Werks") });
}

// ---------------------------------------------------------------------------
// Settings

void NoteWindow::loadSettings()
{
    QSettings settings;
    m_baseFont = QFont(QStringLiteral("Consolas"), 11);
    const QString font = settings.value(QStringLiteral("font")).toString();
    if (!font.isEmpty())
        m_baseFont.fromString(font);
    m_zoom = qBound(kMinZoom, settings.value(QStringLiteral("zoom"), 100).toInt(), kMaxZoom);
    m_statusVisible = settings.value(QStringLiteral("statusBar"), true).toBool();

    m_restoreRadius = settings.value(QStringLiteral("radius"), kDefaultRadius).toInt();
}

void NoteWindow::saveSettings() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("font"), m_baseFont.toString());
    settings.setValue(QStringLiteral("zoom"), m_zoom);
    settings.setValue(QStringLiteral("statusBar"), m_statusVisible);
    settings.setValue(QStringLiteral("radius"), m_maximized || !isVisible() ? m_restoreRadius : m_radius);
}
