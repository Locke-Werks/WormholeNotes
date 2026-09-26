#include "NoteWindow.h"

#include "NoteFace.h"
#include "Round.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QStyleHints>
#include <QTextDocument>
#include <QWheelEvent>
#include <QWindow>

#include <cmath>

namespace {

constexpr int kMargin = 16; // room outside the rim for the shadow and the clip
constexpr int kEdgeGrip = 5;
constexpr int kMinRadius = 120;
constexpr int kDefaultRadius = 210;
constexpr int kMinZoom = 50;
constexpr int kMaxZoom = 300;

// The pushers sit at the upper right of the rim, like a stopwatch's.
constexpr qreal kPusherAngle[] = { -64, -49, -34 };

const QColor kCyan(0x2e, 0xe8, 0xff);
const QColor kViolet(0x6b, 0x3f, 0xd6);

QColor alpha(QColor color, int a)
{
    color.setAlpha(a);
    return color;
}

} // namespace

// ---------------------------------------------------------------------------
// The confirm band: a question written across the middle of the face, with
// its answers as two pills. It covers the face while it is up, so nothing
// else on the note can be touched until it is answered.

class ConfirmBand : public QWidget
{
public:
    explicit ConfirmBand(QWidget *parent)
        : QWidget(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        hide();
    }

    void open(const QString &question, const QString &yes, const QString &no, std::function<void(bool)> answer,
              bool dark)
    {
        m_question = question;
        m_yes = yes;
        m_no = no;
        m_answer = std::move(answer);
        m_dark = dark;
        m_hover = -1;
        show();
        raise();
        setFocus();
        update();
    }

    void answer(bool yes)
    {
        if (!isVisible())
            return;
        hide();
        auto callback = std::move(m_answer);
        m_answer = nullptr;
        if (callback)
            callback(yes);
    }

protected:
    QRectF band() const
    {
        const qreal h = height() * 0.3;
        return QRectF(0, (height() - h) / 2, width(), h);
    }

    QRectF pill(int which) const
    {
        const QRectF b = band();
        const qreal w = width() * 0.26;
        const qreal h = b.height() * 0.3;
        const bool single = m_no.isEmpty();
        const qreal x = single ? (width() - w) / 2 : which == 0 ? width() / 2 - w - 6 : width() / 2 + 6;
        return QRectF(x, b.center().y() + b.height() * 0.06, w, h);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal r = width() / 2.0;
        QPainterPath circle;
        circle.addEllipse(QPointF(r, r), r, r);
        p.setClipPath(circle);
        p.fillRect(rect(), QColor(0, 0, 0, m_dark ? 110 : 60));
        const QRectF b = band();
        p.fillRect(b, m_dark ? QColor(0x1c, 0x19, 0x24, 245) : QColor(0xff, 0xfd, 0xf6, 248));
        p.setPen(QPen(alpha(kViolet, 160), 1.5));
        p.drawLine(b.topLeft(), b.topRight());
        p.drawLine(b.bottomLeft(), b.bottomRight());

        QFont font(QStringLiteral("Segoe UI Variable Text"));
        font.setPixelSize(qMax(11, qRound(r * 0.08)));
        p.setFont(font);
        p.setPen(m_dark ? QColor(0xec, 0xe6, 0xf5) : QColor(0x2a, 0x24, 0x33));
        p.drawText(QRectF(b.left(), b.top(), b.width(), b.height() * 0.5), Qt::AlignCenter, m_question);

        font.setWeight(QFont::DemiBold);
        p.setFont(font);
        for (int i = 0; i < (m_no.isEmpty() ? 1 : 2); ++i) {
            const QRectF box = pill(i);
            const bool primary = i == 0;
            QColor fill = primary ? kViolet : (m_dark ? QColor(0x3a, 0x35, 0x46) : QColor(0xec, 0xe8, 0xf4));
            if (m_hover == i)
                fill = fill.lighter(primary ? 120 : 106);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRoundedRect(box, box.height() / 2, box.height() / 2);
            p.setPen(primary ? QColor(Qt::white) : (m_dark ? QColor(0xec, 0xe6, 0xf5) : QColor(0x2a, 0x24, 0x33)));
            p.drawText(box, Qt::AlignCenter, primary ? m_yes : m_no);
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        int hover = -1;
        for (int i = 0; i < (m_no.isEmpty() ? 1 : 2); ++i) {
            if (pill(i).contains(event->position()))
                hover = i;
        }
        if (hover != m_hover) {
            m_hover = hover;
            setCursor(hover >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        if (pill(0).contains(event->position()))
            answer(true);
        else if (!m_no.isEmpty() && pill(1).contains(event->position()))
            answer(false);
        else if (!band().contains(event->position()))
            answer(false);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape)
            answer(false);
        else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
            answer(m_no.isEmpty());
        else if (event->key() == Qt::Key_Y || event->key() == Qt::Key_D)
            answer(true);
        else if (event->key() == Qt::Key_N || event->key() == Qt::Key_K)
            answer(false);
    }

private:
    QString m_question;
    QString m_yes;
    QString m_no;
    std::function<void(bool)> m_answer;
    bool m_dark = false;
    int m_hover = -1;
};

// ---------------------------------------------------------------------------

NoteWindow::NoteWindow(QWidget *parent)
    : QWidget(parent)
{
    // A tool window, so the note has no taskbar button of its own: it belongs
    // to whatever window its hole is on.
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);

    m_face = new NoteFace(this);
    m_band = new ConfirmBand(this);
    setFocusProxy(m_face);

    connect(m_face->document(), &QTextDocument::contentsChanged, this, [this] { emit textEdited(m_face->text()); });
    connect(m_face, &NoteFace::sheetTurnRequested, this, &NoteWindow::sheetTurnRequested);
    connect(m_face, &NoteFace::zoomRequested, this, &NoteWindow::zoomBy);
    connect(m_face, &NoteFace::contextMenuRequested, this, [this](const QPoint &at) { m_menu->popup(at); });
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        applyTheme();
        update();
    });

    m_menu = new QMenu(this);
    QAction *undo = m_menu->addAction(tr("Undo\tCtrl+Z"), m_face, &NoteFace::undo);
    QAction *redo = m_menu->addAction(tr("Redo\tCtrl+Y"), m_face, &NoteFace::redo);
    m_menu->addSeparator();
    QAction *cut = m_menu->addAction(tr("Cut\tCtrl+X"), m_face, &NoteFace::cut);
    QAction *copy = m_menu->addAction(tr("Copy\tCtrl+C"), m_face, &NoteFace::copy);
    m_menu->addAction(tr("Paste\tCtrl+V"), m_face, &NoteFace::paste);
    QAction *remove = m_menu->addAction(tr("Delete"), m_face, &NoteFace::deleteSelection);
    m_menu->addAction(tr("Select All\tCtrl+A"), m_face, &NoteFace::selectAll);
    m_menu->addSeparator();
    m_menu->addAction(tr("New Sheet\tCtrl+N"), this, [this] { pressPusher(NewPusher); });
    m_menu->addAction(tr("Delete Sheet"), this, [this] { pressPusher(DeletePusher); });
    m_menu->addAction(tr("Put Away\tEsc"), this, [this] { pressPusher(AwayPusher); });
    m_menu->addSeparator();
    m_menu->addAction(tr("About WormholeNotes"), this, &NoteWindow::about);
    m_menu->addAction(tr("Quit WormholeNotes\tCtrl+Q"), this, &NoteWindow::quitRequested);
    connect(m_menu, &QMenu::aboutToShow, this, [=, this] {
        QTextDocument *doc = m_face->document();
        const bool selection = m_face->textCursor().hasSelection();
        undo->setEnabled(doc->isUndoAvailable());
        redo->setEnabled(doc->isRedoAvailable());
        cut->setEnabled(selection);
        copy->setEnabled(selection);
        remove->setEnabled(selection);
    });

    const auto shortcut = [this](const QKeySequence &keys, auto &&slot) {
        connect(new QShortcut(keys, this), &QShortcut::activated, this, slot);
    };
    shortcut(QKeySequence::New, [this] { pressPusher(NewPusher); });
    shortcut(QKeySequence(tr("Ctrl+Q")), [this] { emit quitRequested(); });
    shortcut(QKeySequence::ZoomIn, [this] { zoomBy(1); });
    shortcut(QKeySequence(tr("Ctrl+=")), [this] { zoomBy(1); });
    shortcut(QKeySequence::ZoomOut, [this] { zoomBy(-1); });
    shortcut(QKeySequence(tr("Ctrl+0")), [this] {
        m_zoom = 100;
        applyFont();
    });

    loadSettings();
    applyTheme();
    applyFont();
}

NoteWindow::~NoteWindow()
{
    saveSettings();
}

void NoteWindow::setSheet(const QString &label, const QString &text, int index, int count, bool fromEnd)
{
    m_face->setSheetMarker(index, count);
    m_face->load(text, fromEnd);
    setWindowTitle(label);
    update();
}

void NoteWindow::setRing(const QList<RingPlace> &places, int current)
{
    m_ring = places;
    m_ringCurrent = qBound(0, current, qMax(0, int(places.size()) - 1));
    update();
}

QString NoteWindow::text() const
{
    return m_face->text();
}

void NoteWindow::openAt(const QPoint &globalCenter)
{
    const QScreen *s = QGuiApplication::screenAt(globalCenter);
    if (!s)
        s = QGuiApplication::primaryScreen();
    const QRect area = s->availableGeometry();
    m_radius = qBound(kMinRadius, m_restoreRadius, maximumRadius());
    const int reach = m_radius + kMargin;
    // Out of the hole, but never off the screen.
    const QPoint c(qBound(area.left() + reach, globalCenter.x(), qMax(area.left() + reach, area.right() - reach)),
                   qBound(area.top() + reach, globalCenter.y(), qMax(area.top() + reach, area.bottom() - reach)));
    setCircle(c, m_radius);
    show();
    raise();
    activateWindow();
    m_face->setFocus();
}

void NoteWindow::putAway()
{
    if (!isVisible())
        return;
    m_menu->close();
    m_band->answer(false);
    m_restoreRadius = m_radius;
    saveSettings();
    hide();
}

// Switching to another app puts the note away, the way a sticky note folds
// back when you look at something else. Checked from the event loop, since
// focus passes through nothing on its way to the menu.
void NoteWindow::checkStillActive()
{
    if (!isVisible() || m_menu->isVisible())
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

qreal NoteWindow::rimWidth() const
{
    return qBound(16.0, m_radius * 0.09, 30.0);
}

qreal NoteWindow::faceRadius() const
{
    return m_radius - rimWidth();
}

qreal NoteWindow::rimMid() const
{
    return m_radius - rimWidth() / 2;
}

qreal NoteWindow::pusherRadius() const
{
    return rimWidth() * 0.36;
}

QPointF NoteWindow::pusherCenter(Pusher pusher) const
{
    return Round::polar(center(), rimMid(), Round::radians(kPusherAngle[pusher]));
}

int NoteWindow::maximumRadius() const
{
    const QScreen *s = screen() ? screen() : QGuiApplication::primaryScreen();
    const QRect area = s->availableGeometry();
    return qMax(kMinRadius, qMin(area.width(), area.height()) / 2 - kMargin);
}

void NoteWindow::setCircle(const QPoint &globalCenter, int radius)
{
    m_radius = qBound(kMinRadius, radius, maximumRadius());
    const int side = 2 * (m_radius + kMargin);
    setGeometry(globalCenter.x() - side / 2, globalCenter.y() - side / 2, side, side);
    update();
}

void NoteWindow::resizeEvent(QResizeEvent *)
{
    // The OS can resize the window too (a DPI change), so the radius follows.
    m_radius = width() / 2 - kMargin;
    const int side = 2 * int(std::floor(faceRadius()));
    const QPointF c = center();
    const QRect face(qRound(c.x() - side / 2.0), qRound(c.y() - side / 2.0), side, side);
    m_face->setGeometry(face);
    m_band->setGeometry(face);
}

NoteWindow::Zone NoteWindow::zoneAt(const QPointF &pos, int *index) const
{
    if (m_ring.size() > 1 && QLineF(clipCenter(), pos).length() <= 12)
        return Zone::Clip;
    const qreal d = QLineF(center(), pos).length();
    if (d > m_radius)
        return Zone::Outside;
    if (d < faceRadius())
        return Zone::Face;
    for (int i = 0; i < PusherCount; ++i) {
        if (QLineF(pusherCenter(Pusher(i)), pos).length() <= pusherRadius() + 2) {
            if (index)
                *index = i;
            return Zone::Pusher;
        }
    }
    if (d >= m_radius - kEdgeGrip)
        return Zone::Edge;
    return Zone::Rim;
}

// ---------------------------------------------------------------------------
// The ring of places

// Evenly round the rim, clockwise from noon, where the desktop always sits.
qreal NoteWindow::placeAngle(int index) const
{
    const int n = qMax(1, int(m_ring.size()));
    return Round::kNoon + 2 * 3.14159265358979 * index / n;
}

qreal NoteWindow::clipAngle() const
{
    return m_clipDragging ? m_clipDragAngle : placeAngle(m_ringCurrent);
}

QPointF NoteWindow::clipCenter() const
{
    return Round::polar(center(), m_radius - 2, clipAngle());
}

int NoteWindow::nearestPlace(qreal angle) const
{
    int best = 0;
    qreal bestDistance = 10;
    for (int i = 0; i < m_ring.size(); ++i) {
        const qreal d = Round::between(angle, placeAngle(i));
        if (d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

void NoteWindow::turnToPlace(int index)
{
    if (m_ring.isEmpty())
        return;
    const int n = int(m_ring.size());
    index = (index % n + n) % n;
    if (index == m_ringCurrent)
        return;
    m_ringCurrent = index;
    update();
    emit placeTurned(m_ring.at(index).key);
}

// ---------------------------------------------------------------------------
// Painting

NoteWindow::Theme NoteWindow::theme() const
{
    Theme t;
    t.dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    if (t.dark) {
        t.rimTop = QColor(0x58, 0x3f, 0xb8);
        t.rimBottom = QColor(0x22, 0x17, 0x5c);
    } else {
        t.rimTop = QColor(0x7d, 0x5c, 0xe8);
        t.rimBottom = QColor(0x3b, 0x28, 0x91);
    }
    t.rimInk = QColor(0xfb, 0xf8, 0xff);
    t.rimDim = QColor(0xfb, 0xf8, 0xff, 175);
    t.lip = alpha(kCyan, t.dark ? 150 : 190);
    if (!isActiveWindow()) {
        t.rimTop = t.rimTop.darker(118);
        t.rimBottom = t.rimBottom.darker(118);
    }
    return t;
}

void NoteWindow::applyTheme()
{
    const bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    NoteFace::Colors c;
    if (dark) {
        c.paper = QColor(0x2b, 0x27, 0x33);
        c.paperEdge = QColor(0x21, 0x1e, 0x28);
        c.ink = QColor(0xec, 0xe6, 0xf5);
        c.rule = QColor(255, 255, 255, 20);
        c.selection = alpha(kCyan, 70);
        c.caret = kCyan;
        c.control = QColor(0xec, 0xe6, 0xf5, 170);
    } else {
        c.paper = QColor(0xff, 0xf7, 0xdf);
        c.paperEdge = QColor(0xf0, 0xe4, 0xc0);
        c.ink = QColor(0x2a, 0x24, 0x33);
        c.rule = QColor(0x3c, 0x50, 0xa0, 30);
        c.selection = alpha(kCyan, 95);
        c.caret = kViolet;
        c.control = QColor(0x2a, 0x24, 0x33, 170);
    }
    c.controlHot = alpha(kViolet, 45);
    m_face->setColors(c);
}

void NoteWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Theme t = theme();
    const QPointF c = center();
    const qreal r = m_radius;

    // A soft shadow, since a frameless window gets none of its own.
    QRadialGradient shadow(c + QPointF(0, 4), r + kMargin - 2);
    const qreal edge = r / (r + kMargin - 2);
    shadow.setColorAt(0, QColor(0, 0, 0, 70));
    shadow.setColorAt(edge, QColor(0, 0, 0, 70));
    shadow.setColorAt(1, QColor(0, 0, 0, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(shadow);
    p.drawEllipse(c + QPointF(0, 4), r + kMargin - 2, r + kMargin - 2);

    // The rim, lit from above.
    QLinearGradient rim(c.x(), c.y() - r, c.x(), c.y() + r);
    rim.setColorAt(0, t.rimTop);
    rim.setColorAt(1, t.rimBottom);
    p.setBrush(rim);
    p.drawEllipse(c, r, r);

    // Where the rim meets the paper, a thin line of the wormhole's glow.
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(t.lip, 1.4));
    p.drawEllipse(c, faceRadius() + 0.7, faceRadius() + 0.7);

    // The place's name across the top of the rim, or the place the clip is
    // being dragged to.
    QFont font(QStringLiteral("Segoe UI Variable Text"));
    font.setPixelSize(qMax(9, qRound(rimWidth() * 0.5)));
    font.setWeight(QFont::DemiBold);
    const QString name = m_clipDragging ? m_clipHint : windowTitle();
    // Centred left of noon, so it ends before the pushers begin.
    const qreal span = Round::radians(78);
    Round::arcText(p, c, rimMid(), Round::kNoon - Round::radians(22),
                   Round::fitArc(name, font, rimMid(), span), font, m_clipDragging ? t.rimInk : t.rimDim);

    for (int i = 0; i < PusherCount; ++i)
        drawPusher(p, Pusher(i), t);
    drawRing(p, t);
}

void NoteWindow::drawPusher(QPainter &p, Pusher pusher, const Theme &t) const
{
    const QPointF pc = pusherCenter(pusher);
    const qreal pr = pusherRadius();
    const bool hot = m_hoverPusher == pusher;
    const bool down = hot && m_pressedPusher == pusher;

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, down ? 90 : hot ? 55 : 22));
    p.drawEllipse(pc, pr, pr);

    const qreal s = pr * 0.5;
    p.setPen(QPen(t.rimInk, qMax(1.3, pr * 0.16), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    switch (pusher) {
    case NewPusher:
        p.drawLine(pc + QPointF(-s, 0), pc + QPointF(s, 0));
        p.drawLine(pc + QPointF(0, -s), pc + QPointF(0, s));
        break;
    case DeletePusher:
        p.drawLine(pc + QPointF(-s, 0), pc + QPointF(s, 0));
        break;
    case AwayPusher:
        // A small circle shrinking into the hole it came from.
        p.drawEllipse(pc, s * 0.8, s * 0.8);
        p.setBrush(t.rimInk);
        p.drawEllipse(pc, s * 0.25, s * 0.25);
        break;
    case PusherCount:
        break;
    }
}

void NoteWindow::drawRing(QPainter &p, const Theme &t) const
{
    if (m_ring.size() < 2)
        return;
    const QPointF c = center();

    // A notch on the rim for every open place, bright where there is writing.
    for (int i = 0; i < m_ring.size(); ++i) {
        const QPointF at = Round::polar(c, m_radius - 3, placeAngle(i));
        p.setPen(Qt::NoPen);
        p.setBrush(m_ring.at(i).written ? kCyan : t.rimDim);
        const qreal dot = m_ring.at(i).written ? 2.2 : 1.5;
        p.drawEllipse(at, dot, dot);
    }

    // The binder clip, gripping the rim at the current place. Drawn turned to
    // the radius: local -y points out from the centre.
    const qreal a = clipAngle();
    p.save();
    p.translate(Round::polar(c, m_radius, a));
    p.rotate(Round::degrees(a) + 90);

    // The wire handles, folded back out past the rim, behind the body.
    const QColor steel(0xd4, 0xd8, 0xe0);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 90), 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath wire;
    wire.moveTo(-7, -6);
    wire.cubicTo(-9, -16, 9, -16, 7, -6);
    p.drawPath(wire);
    p.setPen(QPen(m_clipDragging ? steel.lighter(115) : steel, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(wire);

    // The black jaw, gripping the rim: wide where it bites, narrower inside.
    QLinearGradient body(-12, 0, 12, 0);
    body.setColorAt(0, QColor(0x18, 0x18, 0x1c));
    body.setColorAt(0.45, QColor(0x4a, 0x4a, 0x52));
    body.setColorAt(1, QColor(0x10, 0x10, 0x14));
    p.setPen(QPen(QColor(0, 0, 0, 160), 0.8));
    p.setBrush(body);
    QPainterPath jaw;
    jaw.moveTo(-12, -7);
    jaw.lineTo(12, -7);
    jaw.lineTo(8, 4);
    jaw.lineTo(-8, 4);
    jaw.closeSubpath();
    p.drawPath(jaw);
    // The steel band where the handles hinge.
    p.setPen(QPen(steel, 1.2, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(-11, -6), QPointF(11, -6));
    p.restore();
}

// ---------------------------------------------------------------------------
// Mouse and keyboard on the rim

void NoteWindow::mousePressEvent(QMouseEvent *event)
{
    int index = -1;
    const Zone zone = zoneAt(event->position(), &index);
    if (event->button() == Qt::RightButton && zone != Zone::Outside) {
        m_menu->popup(event->globalPosition().toPoint());
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    switch (zone) {
    case Zone::Pusher:
        m_pressedPusher = index;
        update();
        return;
    case Zone::Clip:
        m_clipDragging = true;
        m_clipDragAngle = Round::angleOf(center(), event->position());
        m_clipHint = m_ring.at(nearestPlace(m_clipDragAngle)).label;
        setCursor(Qt::ClosedHandCursor);
        update();
        return;
    case Zone::Edge:
        m_resizing = true;
        m_resizeCenter = mapToGlobal(center());
        m_resizeOffset = m_radius - QLineF(m_resizeCenter, event->globalPosition()).length();
        return;
    case Zone::Rim:
        if (windowHandle())
            windowHandle()->startSystemMove();
        return;
    case Zone::Face:
    case Zone::Outside:
        break;
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
    if (m_clipDragging) {
        m_clipDragAngle = Round::angleOf(center(), event->position());
        m_clipHint = m_ring.at(nearestPlace(m_clipDragAngle)).label;
        update();
        return;
    }

    int index = -1;
    const Zone zone = zoneAt(event->position(), &index);
    const int pusher = zone == Zone::Pusher ? index : -1;
    if (pusher != m_hoverPusher) {
        m_hoverPusher = pusher;
        update();
    }
    if (zone == Zone::Edge) {
        const QPointF v = event->position() - center();
        const qreal a = std::fmod(Round::degrees(std::atan2(v.y(), v.x())) + 360.0, 180.0);
        setCursor(a < 22.5 || a >= 157.5 ? Qt::SizeHorCursor
                  : a < 67.5             ? Qt::SizeFDiagCursor
                  : a < 112.5            ? Qt::SizeVerCursor
                                         : Qt::SizeBDiagCursor);
    } else if (zone == Zone::Clip) {
        setCursor(Qt::OpenHandCursor);
    } else if (zone == Zone::Pusher) {
        setCursor(Qt::PointingHandCursor);
    } else {
        unsetCursor();
    }
    if (zone == Zone::Pusher) {
        static const char *const tips[] = { "New sheet", "Delete this sheet", "Put away" };
        setToolTip(tr(tips[index]));
    } else if (zone == Zone::Clip && !m_ring.isEmpty()) {
        setToolTip(tr("Turn to another place"));
    } else {
        setToolTip(QString());
    }
}

void NoteWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_resizing) {
        m_resizing = false;
        return;
    }
    if (m_clipDragging) {
        m_clipDragging = false;
        unsetCursor();
        const int nearest = nearestPlace(Round::angleOf(center(), event->position()));
        update();
        turnToPlace(nearest);
        return;
    }
    if (m_pressedPusher >= 0) {
        const int pressed = m_pressedPusher;
        m_pressedPusher = -1;
        update();
        int index = -1;
        if (zoneAt(event->position(), &index) == Zone::Pusher && index == pressed)
            pressPusher(Pusher(index));
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void NoteWindow::leaveEvent(QEvent *)
{
    if (m_hoverPusher != -1) {
        m_hoverPusher = -1;
        update();
    }
}

// The wheel over the rim turns the clip, one place per notch.
void NoteWindow::wheelEvent(QWheelEvent *event)
{
    const Zone zone = zoneAt(event->position());
    if (zone == Zone::Face || zone == Zone::Outside || m_ring.size() < 2) {
        QWidget::wheelEvent(event);
        return;
    }
    const int delta = event->angleDelta().y();
    if ((delta > 0) != (m_wheel > 0))
        m_wheel = 0;
    m_wheel += delta;
    int steps = 0;
    for (; m_wheel >= 120; m_wheel -= 120)
        --steps;
    for (; m_wheel <= -120; m_wheel += 120)
        ++steps;
    if (steps != 0)
        turnToPlace(m_ringCurrent + steps);
    event->accept();
}

void NoteWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        emit putAwayRequested();
        return;
    }
    QWidget::keyPressEvent(event);
}

void NoteWindow::closeEvent(QCloseEvent *event)
{
    // Alt+F4 puts the note away. Quitting is on the menu and the tray.
    event->ignore();
    emit putAwayRequested();
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
// Commands

void NoteWindow::pressPusher(Pusher pusher)
{
    if (asking())
        return;
    switch (pusher) {
    case NewPusher:
        emit newSheetRequested();
        break;
    case DeletePusher:
        confirmDelete();
        break;
    case AwayPusher:
        emit putAwayRequested();
        break;
    case PusherCount:
        break;
    }
}

void NoteWindow::confirmDelete()
{
    if (m_face->document()->isEmpty()) {
        emit deleteSheetRequested();
        return;
    }
    ask(tr("Delete this sheet?"), tr("Delete"), tr("Keep"), [this](bool yes) {
        m_face->setFocus();
        if (yes)
            emit deleteSheetRequested();
    });
}

void NoteWindow::about()
{
    ask(tr("WormholeNotes %1 · Locke Werks\nInspired by Tondo, by Archon").arg(QCoreApplication::applicationVersion()),
        tr("OK"), QString(), [this](bool) { m_face->setFocus(); });
}

void NoteWindow::ask(const QString &question, const QString &yes, const QString &no, std::function<void(bool)> answer)
{
    m_band->open(question, yes, no, std::move(answer), theme().dark);
}

bool NoteWindow::asking() const
{
    return m_band->isVisible();
}

void NoteWindow::zoomBy(int steps)
{
    m_zoom = qBound(kMinZoom, m_zoom + steps * 10, kMaxZoom);
    applyFont();
}

// A bigger note holds more words, like a bigger card; the zoom is what makes
// the letters bigger.
void NoteWindow::applyFont()
{
    QFont font(QStringLiteral("Segoe UI Variable Text"));
    font.setPointSizeF(11.0 * m_zoom / 100.0);
    m_face->setTextFont(font);
}

void NoteWindow::loadSettings()
{
    QSettings settings;
    m_zoom = qBound(kMinZoom, settings.value(QStringLiteral("note/zoom"), 100).toInt(), kMaxZoom);
    m_restoreRadius = settings.value(QStringLiteral("note/radius"), kDefaultRadius).toInt();
    m_radius = m_restoreRadius;
}

void NoteWindow::saveSettings() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("note/zoom"), m_zoom);
    settings.setValue(QStringLiteral("note/radius"), isVisible() ? m_radius : m_restoreRadius);
}
