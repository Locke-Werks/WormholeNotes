#include "Wormhole.h"

#include "HoleWindow.h"
#include "NoteWindow.h"

#include <QApplication>
#include <QMenu>
#include <QSystemTrayIcon>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

Wormhole::Wormhole(QObject *parent)
    : QObject(parent)
{
    m_hole = new HoleWindow;
    m_note = new NoteWindow;

    connect(&m_tracker, &PlaceTracker::placeChanged, this, &Wormhole::onPlaceChanged);
    connect(&m_tracker, &PlaceTracker::anchorMoved, this, &Wormhole::reposition);
    connect(m_hole, &HoleWindow::clicked, this, &Wormhole::openNote);
    connect(m_hole, &HoleWindow::dragFinished, this, &Wormhole::onHoleDragged);
    connect(m_note, &NoteWindow::putAwayRequested, this, &Wormhole::putNoteAway);
    connect(m_note, &NoteWindow::quitRequested, this, &Wormhole::quit);
    connect(m_note, &NoteWindow::textEdited, this, [this](const QString &text) {
        m_store.setText(m_place.key, m_place.label, text);
        m_hole->setFilled(!text.isEmpty());
    });

    m_tray = new QSystemTrayIcon(QApplication::windowIcon(), this);
    m_tray->setToolTip(QStringLiteral("WormholeNotes"));
    auto *menu = new QMenu;
    menu->addAction(tr("Open Note"), this, &Wormhole::openNote);
    menu->addSeparator();
    menu->addAction(tr("Quit WormholeNotes"), this, &Wormhole::quit);
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
            openNote();
    });
}

Wormhole::~Wormhole()
{
    m_store.flush();
    delete m_tray->contextMenu();
    delete m_note;
    delete m_hole;
}

void Wormhole::start()
{
    m_store.load();
    m_tray->show();
    m_tracker.start();
    if (m_place.key.isEmpty()) {
        // Nothing has the foreground yet, or only something transient does.
        // The desktop is always a place, so start there.
        Place desktop;
        desktop.key = QStringLiteral("desktop");
        desktop.label = tr("Desktop");
        onPlaceChanged(desktop);
    }
}

void Wormhole::onPlaceChanged(const Place &place)
{
    // The tracker skips our own windows, so a change here means the user went
    // to another app; the open note belongs to the place they left.
    putNoteAway();
    m_place = place;
    const Sheet sheet = m_store.sheet(place.key);
    m_hole->setFilled(!sheet.text.isEmpty());
    m_hole->setToolTip(place.label);
    reposition();
}

void Wormhole::reposition()
{
    if (m_note->isVisible() || m_hole->isDragging())
        return;
    int dpi = 96;
    const QRect frame = m_tracker.anchorRect(&dpi);
    if (frame.isEmpty() || m_place.key.isEmpty()) {
        m_hole->hide();
        return;
    }
    const QPointF hole = m_store.sheet(m_place.key).hole;
    const qreal scale = dpi / 96.0;
    // Kept on the window even when the window is smaller than it was when the
    // hole was put there.
    const int x = qBound(frame.left() + 12, frame.right() - qRound(hole.x() * scale), frame.right() - 12);
    const int y = qBound(frame.top() + 12, frame.top() + qRound(hole.y() * scale), frame.bottom() - 12);
    m_hole->placeAt(QPoint(x, y));
}

void Wormhole::openNote()
{
    if (m_place.key.isEmpty())
        return;
    const Sheet sheet = m_store.sheet(m_place.key);
    m_note->setSheet(m_place.label, sheet.text);
    // The note grows out of the hole, so it is centred where the hole is.
    const QPoint center = m_hole->isVisible() ? m_hole->geometry().center() : QCursor::pos();
    m_hole->hide();
    m_note->openAt(center);
}

void Wormhole::putNoteAway()
{
    if (!m_note->isVisible())
        return;
    m_store.setText(m_place.key, m_place.label, m_note->text());
    const bool hadFocus = m_note->isActiveWindow();
    m_note->putAway();
    // Hand the foreground back to the window the note came from. Left alone,
    // Windows keeps it on the hidden note, and the next app the user picks is
    // measured against that instead of the place they were on.
    if (hadFocus && !m_place.isDesktop() && IsWindow(HWND(m_place.hwnd)))
        SetForegroundWindow(HWND(m_place.hwnd));
    reposition();
}

void Wormhole::onHoleDragged(const QPoint &c)
{
    int dpi = 96;
    const QRect frame = m_tracker.anchorRect(&dpi);
    if (frame.isEmpty())
        return;
    const qreal scale = dpi / 96.0;
    m_store.setHole(m_place.key, QPointF((frame.right() - c.x()) / scale, (c.y() - frame.top()) / scale));
    reposition();
}

void Wormhole::quit()
{
    putNoteAway();
    m_store.flush();
    QApplication::quit();
}
