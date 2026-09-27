#include "Wormhole.h"

#include "DeskNote.h"
#include "HoleWindow.h"
#include "NoteWindow.h"
#include "Theme.h"

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
    connect(&m_extension, &ExtensionLink::reported, &m_tracker, &PlaceTracker::onExtensionReport);
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
    connect(m_note, &NoteWindow::tearOffRequested, this, &Wormhole::tearOff);
    connect(m_note, &NoteWindow::moved, this, &Wormhole::onNoteMoved);
    connect(m_note, &NoteWindow::colourChosen, this, [this](const QColor &colour) {
        m_store.setColour(m_viewKey, m_sheet, colour == Theme::accent() ? QString() : colour.name());
        updateRing();
        updateHole();
        if (DeskNote *desk = m_desk.value(m_viewKey))
            desk->setColour(placeColour(m_viewKey));
    });
    connect(m_note, &NoteWindow::textEdited, this, [this](const QString &text) {
        const bool wasWritten = m_store.place(m_viewKey).hasWriting();
        m_store.setSheet(m_viewKey, m_viewLabel, m_sheet, text);
        if (wasWritten != m_store.place(m_viewKey).hasWriting())
            updateRing();
        if (DeskNote *desk = m_desk.value(m_viewKey))
            desk->setText(m_store.place(m_viewKey).sheets.constFirst());
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
    qDeleteAll(m_desk);
    delete m_tray->contextMenu();
    delete m_note;
    delete m_hole;
}

void Wormhole::start()
{
    m_store.load();
    m_extension.listen();
    NativeHost::registerForUser();
    for (const QString &key : m_store.deskKeys())
        createDeskNote(key);
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
    m_hole->setColour(placeColour(m_place.key));
}

QColor Wormhole::placeColour(const QString &key) const
{
    const PlaceRecord record = m_store.place(key);
    for (int i = 0; i < record.sheets.size(); ++i) {
        if (!record.sheets.at(i).trimmed().isEmpty())
            return Theme::sheetColour(record.colours.value(i));
    }
    return Theme::sheetColour(record.colours.value(0));
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
    m_note->setSheetColour(Theme::sheetColour(record.colours.value(m_sheet)));
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
    const QString left = m_viewKey;
    QString label = isDesk(key) ? deskLabel(key) : key;
    for (const Place &place : m_tracker.openPlaces()) {
        if (place.key == key)
            label = place.label;
    }
    view(key, label, m_lastSheet.value(key, 0));
    if (isDesk(left))
        settleDesk(left);
}

void Wormhole::updateRing()
{
    QList<RingPlace> ring;
    int current = 0;
    for (const Place &place : m_tracker.openPlaces()) {
        if (place.key == m_viewKey)
            current = int(ring.size());
        ring.append({ place.key, place.label, m_store.place(place.key).hasWriting(), placeColour(place.key) });
        // Desk notes are on the desktop, so they follow it round the ring.
        if (place.isDesktop()) {
            for (const QString &key : m_store.deskKeys()) {
                if (key == m_viewKey)
                    current = int(ring.size());
                ring.append({ key, deskLabel(key), true, placeColour(key) });
            }
        }
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
    if (isDesk(m_viewKey))
        settleDesk(m_viewKey);
    // Hand the foreground back to the window the note came from. Left alone,
    // Windows keeps it on the hidden note, and the next app the user picks is
    // measured against that instead of the place they were on.
    if (hadFocus && !m_place.isDesktop() && IsWindow(HWND(m_place.hwnd)))
        SetForegroundWindow(HWND(m_place.hwnd));
    updateHole();
    reposition();
}

// ---------------------------------------------------------------------------
// Desk notes

bool Wormhole::isDesk(const QString &key) const
{
    return key.startsWith(QLatin1String("desk:"));
}

QString Wormhole::deskLabel(const QString &key) const
{
    for (const QString &sheet : m_store.place(key).sheets) {
        const QString line = sheet.trimmed().section(u'\n', 0, 0).trimmed();
        if (!line.isEmpty())
            return line.length() > 28 ? line.left(27) + QChar(0x2026) : line;
    }
    return tr("Desk note");
}

void Wormhole::createDeskNote(const QString &key)
{
    auto *desk = new DeskNote;
    const PlaceRecord record = m_store.place(key);
    desk->setText(record.sheets.constFirst());
    desk->setColour(placeColour(key));
    desk->centerOn(record.desk);
    connect(desk, &DeskNote::clicked, this, [this, key] { openDeskNote(key); });
    connect(desk, &DeskNote::dropped, this, [this, key] { onDeskNoteDropped(key); });
    m_desk.insert(key, desk);
    desk->show();
}

void Wormhole::openDeskNote(const QString &key)
{
    DeskNote *desk = m_desk.value(key);
    if (!desk)
        return;
    putNoteAway();
    view(key, deskLabel(key), m_lastSheet.value(key, 0));
    desk->hide();
    m_note->openAt(desk->logicalCenter());
}

void Wormhole::settleDesk(const QString &key)
{
    DeskNote *desk = m_desk.value(key);
    if (!desk)
        return;
    const PlaceRecord record = m_store.place(key);
    if (!record.hasWriting()) {
        m_store.forget(key);
        m_desk.remove(key);
        desk->deleteLater();
        return;
    }
    desk->setText(record.sheets.constFirst());
    desk->setColour(placeColour(key));
    desk->show();
}

void Wormhole::onDeskNoteDropped(const QString &key)
{
    DeskNote *desk = m_desk.value(key);
    if (!desk)
        return;
    Place target;
    const quintptr hwnd = m_tracker.windowAt(desk->physicalCenter());
    if (!hwnd || !m_tracker.placeOf(hwnd, &target)) {
        // Still on the desktop: it only moved.
        m_store.setDesk(key, desk->logicalCenter());
        return;
    }
    // Dropped on a window: its writing joins that place's sheets.
    const PlaceRecord dropped = m_store.place(key);
    for (int i = 0; i < dropped.sheets.size(); ++i) {
        if (dropped.sheets.at(i).trimmed().isEmpty())
            continue;
        const PlaceRecord there = m_store.place(target.key);
        const int index = there.sheets.size() == 1 && there.sheets.constFirst().trimmed().isEmpty()
            ? 0
            : m_store.addSheet(target.key, target.label);
        m_store.setSheet(target.key, target.label, index, dropped.sheets.at(i));
        m_store.setColour(target.key, index, dropped.colours.value(i));
    }
    m_store.forget(key);
    m_desk.remove(key);
    desk->deleteLater();
    if (target.key == m_place.key)
        updateHole();
}

void Wormhole::tearOff()
{
    if (isDesk(m_viewKey))
        return;
    const QString text = m_note->text();
    if (text.trimmed().isEmpty())
        return;
    const QPoint at = m_note->geometry().center();
    const QString key = m_store.newDeskNote({ text }, at, { m_store.place(m_viewKey).colours.value(m_sheet) });
    // The sheet leaves the page, which is left with a fresh blank if that
    // was its only one.
    m_store.removeSheet(m_viewKey, m_sheet);
    m_sheet = qMin(m_sheet, int(m_store.place(m_viewKey).sheets.size()) - 1);
    m_lastSheet.insert(m_viewKey, m_sheet);
    createDeskNote(key);
    putNoteAway();
}

// A note dropped where only the desktop shows is torn off there. Moving it
// aside over another window is just moving it.
void Wormhole::onNoteMoved()
{
    if (!m_note->isVisible() || isDesk(m_viewKey))
        return;
    if (m_tracker.windowAt(m_note->physicalCenter()) == 0)
        tearOff();
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
