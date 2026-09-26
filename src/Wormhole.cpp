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
    connect(&m_tracker, &PlaceTracker::placeClosed, this, &Wormhole::onPlaceClosed);
    connect(&m_tracker, &PlaceTracker::openPlacesChanged, this, [this] {
        if (m_note->isVisible())
            updateRing();
    });
    connect(m_hole, &HoleWindow::clicked, this, &Wormhole::openNote);
    connect(m_hole, &HoleWindow::dragFinished, this, &Wormhole::onHoleDragged);
    connect(m_note, &NoteWindow::putAwayRequested, this, &Wormhole::putNoteAway);
    connect(m_note, &NoteWindow::quitRequested, this, &Wormhole::quit);
    connect(m_note, &NoteWindow::sheetTurnRequested, this, &Wormhole::turnSheet);
    connect(m_note, &NoteWindow::newSheetRequested, this, &Wormhole::newSheet);
    connect(m_note, &NoteWindow::deleteSheetRequested, this, &Wormhole::deleteSheet);
    connect(m_note, &NoteWindow::placeTurned, this, &Wormhole::turnPlace);
    connect(m_note, &NoteWindow::textEdited, this, [this](const QString &text) {
        const bool wasWritten = m_store.place(m_viewKey).hasWriting();
        m_store.setSheet(m_viewKey, m_viewLabel, m_sheet, text);
        if (wasWritten != m_store.place(m_viewKey).hasWriting())
            updateRing();
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
        onPlaceChanged(m_tracker.openPlaces().constFirst());
    }
}

void Wormhole::onPlaceChanged(const Place &place)
{
    // The tracker skips our own windows, so a change here means the user went
    // to another app; the open note belongs to the place they left.
    putNoteAway();
    m_place = place;
    m_hole->setToolTip(place.label);
    updateHole();
    reposition();
}

void Wormhole::onPlaceClosed(const QString &key)
{
    if (m_note->isVisible() && key == m_viewKey)
        return;
    m_store.dropBlanks(key);
    m_lastSheet.remove(key);
}

void Wormhole::updateHole()
{
    m_hole->setFilled(m_store.place(m_place.key).hasWriting());
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
    const QPointF hole = m_store.place(m_place.key).hole;
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
    view(m_place.key, m_place.label, m_lastSheet.value(m_place.key, 0));
    // The note grows out of the hole, so it is centred where the hole is.
    const QPoint center = m_hole->isVisible() ? m_hole->geometry().center() : QCursor::pos();
    m_hole->hide();
    m_note->openAt(center);
}

void Wormhole::view(const QString &key, const QString &label, int sheet, bool fromEnd)
{
    m_viewKey = key;
    m_viewLabel = label;
    showSheet(sheet, fromEnd);
    updateRing();
}

void Wormhole::showSheet(int index, bool fromEnd)
{
    const PlaceRecord record = m_store.place(m_viewKey);
    m_sheet = qBound(0, index, int(record.sheets.size()) - 1);
    m_lastSheet.insert(m_viewKey, m_sheet);
    m_note->setSheet(m_viewLabel, record.sheets.at(m_sheet), m_sheet, int(record.sheets.size()), fromEnd);
}

void Wormhole::leaveSheet()
{
    const PlaceRecord record = m_store.place(m_viewKey);
    if (record.sheets.size() > 1 && m_sheet < record.sheets.size() && record.sheets.at(m_sheet).trimmed().isEmpty())
        m_store.removeSheet(m_viewKey, m_sheet);
}

void Wormhole::turnSheet(int direction)
{
    const int count = int(m_store.place(m_viewKey).sheets.size());
    int target = m_sheet + direction;
    if (target < 0)
        return;
    if (target >= count) {
        newSheet();
        return;
    }
    const bool leavingBlank = count > 1 && m_store.place(m_viewKey).sheets.at(m_sheet).trimmed().isEmpty();
    leaveSheet();
    if (leavingBlank && target > m_sheet)
        --target;
    showSheet(target, direction < 0);
}

void Wormhole::newSheet()
{
    const PlaceRecord record = m_store.place(m_viewKey);
    // A blank last sheet already is the new sheet.
    if (record.sheets.constLast().trimmed().isEmpty()) {
        showSheet(int(record.sheets.size()) - 1, false);
        return;
    }
    leaveSheet();
    showSheet(m_store.addSheet(m_viewKey, m_viewLabel), false);
}

void Wormhole::deleteSheet()
{
    m_store.removeSheet(m_viewKey, m_sheet);
    showSheet(qMin(m_sheet, int(m_store.place(m_viewKey).sheets.size()) - 1), false);
    updateRing();
}

void Wormhole::turnPlace(const QString &key)
{
    leaveSheet();
    QString label = key;
    for (const Place &place : m_tracker.openPlaces()) {
        if (place.key == key)
            label = place.label;
    }
    view(key, label, m_lastSheet.value(key, 0));
}

void Wormhole::updateRing()
{
    QList<RingPlace> ring;
    int current = 0;
    for (const Place &place : m_tracker.openPlaces()) {
        if (place.key == m_viewKey)
            current = int(ring.size());
        ring.append({ place.key, place.label, m_store.place(place.key).hasWriting() });
    }
    m_note->setRing(ring, current);
}

void Wormhole::putNoteAway()
{
    if (!m_note->isVisible())
        return;
    leaveSheet();
    const bool hadFocus = m_note->isActiveWindow();
    m_note->putAway();
    // Hand the foreground back to the window the note came from. Left alone,
    // Windows keeps it on the hidden note, and the next app the user picks is
    // measured against that instead of the place they were on.
    if (hadFocus && !m_place.isDesktop() && IsWindow(HWND(m_place.hwnd)))
        SetForegroundWindow(HWND(m_place.hwnd));
    updateHole();
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
