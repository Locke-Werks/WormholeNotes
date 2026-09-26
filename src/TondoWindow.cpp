#include "TondoWindow.h"

#include "Arc.h"
#include "RadialMenu.h"
#include "RadialPrompt.h"
#include "RingTextLayout.h"
#include "RoundEdit.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontDialog>
#include <QGuiApplication>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QPageSetupDialog>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QProcess>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTextBlock>
#include <QTextDocument>
#include <QWindow>

#include <cmath>

namespace {

constexpr int kShadow = 16;
constexpr int kEdgeGrip = 7;
constexpr int kMinRadius = 170;
constexpr int kDefaultRadius = 300;
constexpr int kMinZoom = 10;
constexpr int kMaxZoom = 500;

// Degrees around the bezel, screen convention: 0 is three o'clock, -90 noon.
// The menu bar starts just above nine o'clock and runs clockwise toward noon;
// the title fills the arc between it and the window buttons.
constexpr qreal kMenuBarStart = -172;
constexpr qreal kButtonAngle[] = { -40, -27, -14 };
constexpr qreal kStatusSpan = 120;

const QColor kAccent(0xd6, 0x26, 0x2a);

QString displayName(const QString &path)
{
    return path.isEmpty() ? QObject::tr("Untitled") : QFileInfo(path).fileName();
}

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

} // namespace

TondoWindow::TondoWindow(QWidget *parent)
    : QWidget(parent)
{
    // The minimize and system-menu hints are what let the taskbar button
    // minimize and restore a window that has no frame of its own.
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinimizeButtonHint
                   | Qt::WindowSystemMenuHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setAcceptDrops(true);

    m_edit = new RoundEdit(this);
    m_prompt = new RadialPrompt(this);
    m_radial = new RadialMenu(this);
    setFocusProxy(m_edit);
    m_printer = new QPrinter(QPrinter::HighResolution);

    m_flashTimer.setSingleShot(true);
    connect(&m_flashTimer, &QTimer::timeout, this, [this] {
        m_flash.clear();
        update();
    });

    createActions();

    QTextDocument *doc = m_edit->document();
    connect(doc, &QTextDocument::modificationChanged, this, &TondoWindow::updateTitle);
    connect(doc, &QTextDocument::undoAvailable, this, &TondoWindow::updateActions);
    connect(doc, &QTextDocument::redoAvailable, this, &TondoWindow::updateActions);
    connect(doc, &QTextDocument::contentsChanged, this, &TondoWindow::updateActions);
    connect(m_edit, &RoundEdit::selectionChanged, this, &TondoWindow::updateActions);
    connect(m_edit, &RoundEdit::cursorPositionChanged, this, qOverload<>(&QWidget::update));
    connect(m_edit, &RoundEdit::zoomRequested, this, &TondoWindow::zoomBy);
    connect(m_edit, &RoundEdit::filesDropped, this, [this](const QStringList &paths) {
        openDropped(paths.first());
    });
    connect(m_edit, &RoundEdit::contextMenuRequested, this, [this](qreal angle) {
        m_radial->open(m_contextMenu, angle);
    });
    connect(m_radial, &RadialMenu::closed, this, [this] {
        m_edit->setFocus();
        update();
    });
    connect(m_prompt, &RadialPrompt::buttonClicked, this, &TondoWindow::onPromptButton);
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
    updateTitle();
    updateActions();
}

TondoWindow::~TondoWindow()
{
    delete m_printer;
}

// ---------------------------------------------------------------------------
// Geometry

QPointF TondoWindow::center() const
{
    return QPointF(width() / 2.0, height() / 2.0);
}

int TondoWindow::ringWidth() const
{
    return qBound(28, qRound(m_radius * 0.115), 44);
}

qreal TondoWindow::innerRadius() const
{
    return m_radius - ringWidth();
}

qreal TondoWindow::ringMid() const
{
    return m_radius - ringWidth() / 2.0;
}

qreal TondoWindow::buttonRadius() const
{
    return ringWidth() * 0.34;
}

QPointF TondoWindow::buttonCenter(Button button) const
{
    return Arc::polar(center(), ringMid(), Arc::radians(kButtonAngle[button]));
}

QFont TondoWindow::bezelFont(qreal scale, bool bold) const
{
    QFont font(QStringLiteral("Segoe UI Variable Text"));
    font.setPixelSize(qMax(9, qRound(ringWidth() * scale)));
    if (bold)
        font.setWeight(QFont::DemiBold);
    return font;
}

QList<TondoWindow::Header> TondoWindow::headers() const
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

TondoWindow::Zone TondoWindow::zoneAt(const QPointF &pos, int *index) const
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

int TondoWindow::maximumRadius() const
{
    const QScreen *s = screen() ? screen() : QGuiApplication::primaryScreen();
    const QRect area = s->availableGeometry();
    return qMax(kMinRadius, qMin(area.width(), area.height()) / 2 - kShadow);
}

void TondoWindow::setCircle(const QPoint &globalCenter, int radius)
{
    m_radius = qBound(kMinRadius, radius, maximumRadius());
    const int side = 2 * (m_radius + kShadow);
    setGeometry(globalCenter.x() - side / 2, globalCenter.y() - side / 2, side, side);
    update();
}

void TondoWindow::resizeEvent(QResizeEvent *)
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

TondoWindow::Theme TondoWindow::theme() const
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

void TondoWindow::paintEvent(QPaintEvent *)
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
        const qreal to = Arc::radians(kButtonAngle[MinimizeButton]) - buttonRadius() / mid - Arc::radians(3);
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

void TondoWindow::drawButton(QPainter &p, Button button, const Theme &t) const
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
    case MinimizeButton:
        p.drawLine(QPointF(-s, 0), QPointF(s, 0));
        break;
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

void TondoWindow::mousePressEvent(QMouseEvent *event)
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

void TondoWindow::mouseMoveEvent(QMouseEvent *event)
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

void TondoWindow::mouseReleaseEvent(QMouseEvent *event)
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

void TondoWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && zoneAt(event->position()) == Zone::Ring) {
        toggleMaximize();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void TondoWindow::leaveEvent(QEvent *)
{
    if (m_hoverButton != -1 || m_hoverHeader != -1) {
        m_hoverButton = -1;
        m_hoverHeader = -1;
        update();
    }
}

void TondoWindow::keyPressEvent(QKeyEvent *event)
{
    // Escape in the text closes a Find or Replace ring left open beside it.
    if (event->key() == Qt::Key_Escape && m_prompt->isOpen()) {
        m_prompt->dismiss();
        return;
    }
    QWidget::keyPressEvent(event);
}

void TondoWindow::openMenu(int header, bool fromKeyboard)
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

void TondoWindow::triggerButton(Button button)
{
    switch (button) {
    case MinimizeButton: showMinimized(); break;
    case MaximizeButton: toggleMaximize(); break;
    case CloseButton: close(); break;
    case ButtonCount: break;
    }
}

void TondoWindow::toggleMaximize()
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

void TondoWindow::closeEvent(QCloseEvent *event)
{
    m_radial->close();

    // Never ask inside closeEvent. While a close is being handled Qt accepts
    // any further close request outright, so a second Alt+F4 or a taskbar
    // Close during "Save changes?" would quit and lose the work. Refuse this
    // close, ask from the event loop, then close again once answered.
    if (modalOpen()) {
        event->ignore();
        return;
    }
    if (!m_closeConfirmed && hasUnsavedChanges()) {
        event->ignore();
        if (!m_closePending) {
            m_closePending = true;
            QTimer::singleShot(0, this, [this] {
                const bool proceed = maybeSave();
                m_closePending = false;
                if (proceed) {
                    m_closeConfirmed = true;
                    close();
                }
            });
        }
        return;
    }
    m_prompt->dismiss();
    saveSettings();
    event->accept();
}

bool TondoWindow::hasUnsavedChanges() const
{
    return m_edit->document()->isModified();
}

bool TondoWindow::modalOpen() const
{
    return m_prompt->isOpen() && m_prompt->isModal();
}

void TondoWindow::openDropped(const QString &path)
{
    // Out of the drop handler first: a prompt shown inside IDropTarget::Drop
    // keeps Explorer's drag stuck until it is answered.
    QTimer::singleShot(0, this, [this, path] {
        if (maybeSave())
            openPath(path);
    });
}

void TondoWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::ActivationChange)
        update();
    QWidget::changeEvent(event);
}

// A copy, never the proposed action: accepting a Shift-drag's move makes
// Explorer delete the file that was dropped.
void TondoWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }
}

void TondoWindow::dropEvent(QDropEvent *event)
{
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            event->setDropAction(Qt::CopyAction);
            event->accept();
            openDropped(url.toLocalFile());
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Menus and actions

void TondoWindow::createActions()
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

    QMenu *file = new QMenu(tr("&File"), this);
    add(file, tr("&New"), { QKeySequence::New }, [this] { newFile(); });
    add(file, tr("New &Window"), { QKeySequence(tr("Ctrl+Shift+N")) }, [this] { newWindow(); });
    add(file, tr("&Open..."), { QKeySequence::Open }, [this] { openFile(); });
    add(file, tr("&Save"), { QKeySequence::Save }, [this] { save(); });
    add(file, tr("Save &As..."), { QKeySequence(tr("Ctrl+Shift+S")) }, [this] { saveAs(); });
    file->addSeparator();
    add(file, tr("Page Set&up..."), {}, [this] { pageSetup(); });
    add(file, tr("&Print..."), { QKeySequence::Print }, [this] { print(); });
    file->addSeparator();
    add(file, tr("E&xit"), {}, [this] { close(); });

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
    connect(m_familyMenu, &QMenu::aboutToShow, this, &TondoWindow::rebuildFamilyMenu);
    connect(m_sizeMenu, &QMenu::aboutToShow, this, &TondoWindow::rebuildSizeMenu);
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
    format->addSeparator();

    QMenu *encodingMenu = format->addMenu(tr("&Encoding"));
    m_encodingGroup = new QActionGroup(this);
    for (const Encoding e : { Encoding::Utf8, Encoding::Utf8Bom, Encoding::Utf16LE, Encoding::Utf16BE,
                              Encoding::Ansi }) {
        QAction *action = encodingMenu->addAction(encodingName(e));
        action->setCheckable(true);
        action->setData(int(e));
        m_encodingGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, e] { setEncoding(e); });
    }
    QMenu *eolMenu = format->addMenu(tr("&Line Endings"));
    m_lineEndingGroup = new QActionGroup(this);
    for (const LineEnding e : { LineEnding::CRLF, LineEnding::LF, LineEnding::CR }) {
        QAction *action = eolMenu->addAction(lineEndingName(e));
        action->setCheckable(true);
        action->setData(int(e));
        m_lineEndingGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, e] { setLineEnding(e); });
    }

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
    m_onTopAction = add(view, tr("Always on &Top"), {}, [this](bool on) { setAlwaysOnTop(on); });
    m_onTopAction->setCheckable(true);
    view->addSeparator();
    add(view, tr("Pre&vious Page"), { QKeySequence(Qt::CTRL | Qt::Key_PageUp) },
        [this] { m_edit->showPage(m_edit->currentPage() - 1); });
    add(view, tr("Ne&xt Page"), { QKeySequence(Qt::CTRL | Qt::Key_PageDown) },
        [this] { m_edit->showPage(m_edit->currentPage() + 1); });

    QMenu *help = new QMenu(tr("&Help"), this);
    add(help, tr("&About Tondo"), {}, [this] { about(); });

    m_menus = { file, edit, format, view, help };

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

    // Alt+letter opens that menu from its place on the bezel; F10 opens File.
    const Qt::Key keys[] = { Qt::Key_F, Qt::Key_E, Qt::Key_O, Qt::Key_V, Qt::Key_H };
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
void TondoWindow::rebuildFamilyMenu()
{
    // Monospaced faces first, since this is Notepad, then a few others worth
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

void TondoWindow::rebuildSizeMenu()
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

void TondoWindow::updateActions()
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
    for (QAction *action : m_encodingGroup->actions())
        action->setChecked(action->data().toInt() == int(m_encoding));
    for (QAction *action : m_lineEndingGroup->actions())
        action->setChecked(action->data().toInt() == int(m_lineEnding));
}

void TondoWindow::updateTitle()
{
    const QString marker = m_edit->document()->isModified() ? QStringLiteral("*") : QString();
    setWindowTitle(QStringLiteral("%1%2 - Tondo").arg(marker, displayName(m_path)));
    update();
}

QString TondoWindow::statusText() const
{
    if (!m_flash.isEmpty())
        return m_flash;
    const QTextCursor cursor = m_edit->textCursor();
    QString position = tr("Ln %1, Col %2").arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1);
    if (cursor.hasSelection())
        position += tr(" (%1 selected)").arg(cursor.selectionEnd() - cursor.selectionStart());
    const QStringList parts = { position, QStringLiteral("%1%").arg(m_zoom), lineEndingName(m_lineEnding),
                                encodingName(m_encoding) };
    return parts.join(QStringLiteral("   ·   "));
}

void TondoWindow::flash(const QString &message)
{
    m_flash = message;
    m_flashTimer.start(3500);
    update();
}

void TondoWindow::applyTheme()
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

void TondoWindow::applyFont()
{
    QFont font = m_baseFont;
    const qreal base = m_baseFont.pointSizeF() > 0 ? m_baseFont.pointSizeF() : 11.0;
    font.setPointSizeF(qMax(1.0, base * m_zoom / 100.0));
    m_edit->setFont(font);
    m_edit->setTabStopDistance(QFontMetricsF(font).horizontalAdvance(u' ') * 8);
    updateActions();
}

void TondoWindow::zoomBy(int steps)
{
    setZoom(m_zoom + steps * 10);
}

void TondoWindow::setZoom(int percent)
{
    percent = qBound(kMinZoom, percent, kMaxZoom);
    if (percent == m_zoom)
        return;
    m_zoom = percent;
    applyFont();
    update();
}

void TondoWindow::setAlwaysOnTop(bool on)
{
    const QRect geometry = this->geometry();
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    setGeometry(geometry);
    show();
    m_onTopAction->setChecked(on);
}

// ---------------------------------------------------------------------------
// Prompts

int TondoWindow::ask(const QStringList &lines, const QList<QPair<QString, int>> &buttons, int defaultId,
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

void TondoWindow::tell(const QStringList &lines)
{
    ask(lines, { { tr("OK"), OkId } }, OkId, OkId);
}

// ---------------------------------------------------------------------------
// Files

bool TondoWindow::maybeSave()
{
    if (!m_edit->document()->isModified())
        return true;
    const int answer = ask({ tr("Save changes to %1?").arg(displayName(m_path)) },
                           { { tr("Save"), SaveId }, { tr("Don't Save"), DiscardId }, { tr("Cancel"), CancelId } },
                           SaveId, CancelId);
    if (answer == SaveId)
        return save();
    return answer == DiscardId;
}

void TondoWindow::newFile()
{
    if (!maybeSave())
        return;
    m_edit->clear();
    m_path.clear();
    m_encoding = Encoding::Utf8;
    m_lineEnding = LineEnding::CRLF;
    m_edit->document()->setModified(false);
    updateTitle();
    updateActions();
}

void TondoWindow::newWindow()
{
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
}

void TondoWindow::openFile()
{
    if (!maybeSave())
        return;
    const QString dir = m_path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                         : QFileInfo(m_path).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, tr("Open"), dir,
                                                      tr("Text Documents (*.txt);;All Files (*.*)"));
    if (!path.isEmpty())
        openPath(path);
}

bool TondoWindow::openPath(const QString &path)
{
    TextFile file;
    QString error;
    if (!TextFile::read(path, file, &error)) {
        tell({ tr("Cannot open %1").arg(QFileInfo(path).fileName()), error });
        return false;
    }
    m_edit->setPlainText(file.text);
    m_path = QFileInfo(path).absoluteFilePath();
    m_encoding = file.encoding;
    m_lineEnding = file.lineEnding;
    m_edit->document()->setModified(false);
    updateTitle();
    updateActions();
    return true;
}

bool TondoWindow::save()
{
    if (m_path.isEmpty())
        return saveAs();
    return writeTo(m_path);
}

bool TondoWindow::saveAs()
{
    const QString initial = m_path.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/*.txt")
        : m_path;
    const QString path = QFileDialog::getSaveFileName(this, tr("Save As"), initial,
                                                      tr("Text Documents (*.txt);;All Files (*.*)"));
    if (path.isEmpty())
        return false;
    return writeTo(path);
}

bool TondoWindow::writeTo(const QString &path)
{
    TextFile file;
    file.text = m_edit->exactText();
    file.encoding = m_encoding;
    file.lineEnding = m_lineEnding;

    if (!TextFile::canEncode(file.text, file.encoding)) {
        const int answer = ask({ tr("Some characters cannot be saved as ANSI"),
                                 tr("and would be written as question marks.") },
                               { { tr("Save as UTF-8"), SaveId }, { tr("Save Anyway"), DiscardId },
                                 { tr("Cancel"), CancelId } },
                               SaveId, CancelId);
        if (answer == CancelId)
            return false;
        if (answer == SaveId) {
            m_encoding = file.encoding = Encoding::Utf8;
            updateActions();
        }
    }

    QString error;
    if (!file.write(path, &error)) {
        tell({ tr("Cannot save %1").arg(QFileInfo(path).fileName()), error });
        return false;
    }
    m_path = QFileInfo(path).absoluteFilePath();
    m_edit->document()->setModified(false);
    updateTitle();
    return true;
}

void TondoWindow::setEncoding(Encoding encoding)
{
    if (encoding != m_encoding) {
        m_encoding = encoding;
        m_edit->document()->setModified(true);
    }
    updateActions();
    update();
}

void TondoWindow::setLineEnding(LineEnding lineEnding)
{
    if (lineEnding != m_lineEnding) {
        m_lineEnding = lineEnding;
        m_edit->document()->setModified(true);
    }
    updateActions();
    update();
}

// ---------------------------------------------------------------------------
// Printing

void TondoWindow::pageSetup()
{
    QPageSetupDialog dialog(m_printer, this);
    dialog.exec();
}

void TondoWindow::print()
{
    QPrintDialog dialog(m_printer, this);
    if (dialog.exec() == QDialog::Accepted)
        printTo(m_printer);
}

void TondoWindow::printTo(QPrinter *printer)
{
    RingTextLayout *layout = m_edit->ringLayout();
    const qreal d = 2 * layout->outerRadius();
    if (d <= 0)
        return;

    QPainter p;
    if (!p.begin(printer)) {
        tell({ tr("The printer could not be started.") });
        return;
    }

    // The layout is in logical pixels, 96 to the inch. Print at the same
    // physical size as on screen, shrinking only if a circle will not fit.
    const QRectF area(QPointF(0, 0), printer->pageLayout().paintRectPixels(printer->resolution()).size());
    const qreal footer = printer->resolution() * 0.35;
    const qreal scale = qMin(printer->resolution() / 96.0, qMin(area.width(), area.height() - 2 * footer) / d);
    const int pages = layout->pageCount();

    QFont footerFont(QStringLiteral("Segoe UI"));
    footerFont.setPointSizeF(9);

    for (int page = 0; page < pages; ++page) {
        if (page > 0)
            printer->newPage();
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(area.center() - QPointF(0, footer / 2));
        p.scale(scale, scale);
        p.setPen(QPen(QColor(0, 0, 0, 90), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(0, 0), d / 2 - 0.5, d / 2 - 0.5);
        p.drawEllipse(QPointF(0, 0), layout->hubRadius() - 4, layout->hubRadius() - 4);
        layout->paintPage(&p, page, Qt::black, Qt::transparent, 0, 0);
        p.restore();

        p.setFont(footerFont);
        p.setPen(Qt::black);
        p.drawText(QRectF(area.left(), area.bottom() - footer, area.width(), footer), Qt::AlignCenter,
                   tr("%1  ·  %2 of %3").arg(displayName(m_path)).arg(page + 1).arg(pages));
    }
    p.end();
}

// ---------------------------------------------------------------------------
// Find, replace, go to

void TondoWindow::showFind(bool replaceMode)
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

SearchOptions TondoWindow::promptOptions() const
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

void TondoWindow::onPromptButton(int id)
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

bool TondoWindow::findNext(bool backward)
{
    if (m_lastSearch.needle.isEmpty()) {
        showFind(false);
        return false;
    }
    SearchOptions options = m_promptMode != PromptMode::None ? promptOptions() : m_lastSearch;
    options.backward = backward;
    return findWith(options);
}

bool TondoWindow::findWith(const SearchOptions &options)
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

void TondoWindow::replaceOne()
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

void TondoWindow::replaceAll()
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

void TondoWindow::goToLine()
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

void TondoWindow::insertTimeDate()
{
    const QLocale locale = QLocale::system();
    const QDateTime now = QDateTime::currentDateTime();
    m_edit->insertPlainText(locale.toString(now.time(), QLocale::ShortFormat) + u' '
                            + locale.toString(now.date(), QLocale::ShortFormat));
}

void TondoWindow::chooseFont()
{
    bool ok = false;
    const QFont font = QFontDialog::getFont(&ok, m_baseFont, this, tr("Font"));
    if (ok) {
        m_baseFont = font;
        applyFont();
    }
}

void TondoWindow::about()
{
    tell({ tr("Tondo %1").arg(QCoreApplication::applicationVersion()), tr("A notepad in the round."),
           tr("Locke Werks") });
}

// ---------------------------------------------------------------------------
// Settings

void TondoWindow::loadSettings()
{
    QSettings settings;
    m_baseFont = QFont(QStringLiteral("Consolas"), 11);
    const QString font = settings.value(QStringLiteral("font")).toString();
    if (!font.isEmpty())
        m_baseFont.fromString(font);
    m_zoom = qBound(kMinZoom, settings.value(QStringLiteral("zoom"), 100).toInt(), kMaxZoom);
    m_statusVisible = settings.value(QStringLiteral("statusBar"), true).toBool();

    QPoint c = settings.value(QStringLiteral("center")).toPoint();
    if (!settings.contains(QStringLiteral("center")) || !QGuiApplication::screenAt(c))
        c = QGuiApplication::primaryScreen()->availableGeometry().center();
    setCircle(c, settings.value(QStringLiteral("radius"), kDefaultRadius).toInt());

    if (settings.value(QStringLiteral("alwaysOnTop"), false).toBool()) {
        setWindowFlag(Qt::WindowStaysOnTopHint, true);
        m_onTopAction->setChecked(true);
    }
}

void TondoWindow::saveSettings() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("font"), m_baseFont.toString());
    settings.setValue(QStringLiteral("zoom"), m_zoom);
    settings.setValue(QStringLiteral("statusBar"), m_statusVisible);
    settings.setValue(QStringLiteral("alwaysOnTop"), m_onTopAction->isChecked());
    settings.setValue(QStringLiteral("center"), m_maximized ? m_restoreCenter : mapToGlobal(center()).toPoint());
    settings.setValue(QStringLiteral("radius"), m_maximized ? m_restoreRadius : m_radius);
}
