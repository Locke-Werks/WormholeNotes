#include "Wormhole.h"

#include "DeskNote.h"
#include "HoleWindow.h"
#include "Hotkey.h"
#include "NoteWindow.h"
#include "SearchWindow.h"
#include "TearTarget.h"
#include "Theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QMenu>
#include <QScreen>
#include <QSettings>
#include <QFileDialog>
#include <QSaveFile>
#include <QStandardPaths>

#include <cmath>
#include <QSystemTrayIcon>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

Wormhole::Wormhole(QObject *parent)
    : QObject(parent)
{
    m_hole = new HoleWindow;
    m_note = new NoteWindow;
    m_target = new TearTarget;
    m_search = new SearchWindow;
    connect(m_search, &SearchWindow::queryChanged, this, &Wormhole::runSearch);
    connect(m_search, &SearchWindow::chosen, this, &Wormhole::openHit);

    connect(&m_tracker, &PlaceTracker::placeChanged, this, &Wormhole::onPlaceChanged);
    connect(&m_tracker, &PlaceTracker::anchorMoved, this, &Wormhole::reposition);
    connect(&m_tracker, &PlaceTracker::placeClosed, this, &Wormhole::onPlaceClosed);
    connect(&m_extension, &ExtensionLink::reported, &m_tracker, &PlaceTracker::onExtensionReport);
    connect(&m_tracker, &PlaceTracker::openPlacesChanged, this, [this] {
        if (m_note->isVisible())
            updateRing();
    });
    connect(m_hole, &HoleWindow::clicked, this, &Wormhole::openNote);
    connect(m_hole, &HoleWindow::dragStarted, this, [this] {
        // A hole with writing behind it can be carried to the tear-off target
        // too; the sheet it opens on goes to the desktop.
        if (m_store.place(m_place.key).hasWriting())
            m_target->appear(m_hole->geometry().center(), placeColour(m_place.key));
    });
    connect(m_hole, &HoleWindow::dragging, m_target, &TearTarget::track);
    connect(m_hole, &HoleWindow::dragFinished, this, &Wormhole::onHoleDragged);
    connect(m_note, &NoteWindow::putAwayRequested, this, &Wormhole::putNoteAway);
    connect(m_note, &NoteWindow::quitRequested, this, &Wormhole::quit);
    connect(m_note, &NoteWindow::sheetTurnRequested, this, &Wormhole::turnSheet);
    connect(m_note, &NoteWindow::newSheetRequested, this, &Wormhole::newSheet);
    connect(m_note, &NoteWindow::deleteSheetRequested, this, &Wormhole::deleteSheet);
    connect(m_note, &NoteWindow::placeTurned, this, &Wormhole::turnPlace);
    connect(m_note, &NoteWindow::tearOffRequested, this, [this] { tearOff(); });
    connect(m_note, &NoteWindow::moveStarted, this, [this] {
        if (!isDesk(m_viewKey))
            m_target->appear(m_note->geometry().center(), m_note->sheetColour());
    });
    connect(m_note, &NoteWindow::moving, m_target, &TearTarget::track);
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
    // Search reaches from any app on a hotkey, since the tray icon is often
    // tucked away with the hidden ones.
    m_hotkey = new Hotkey(this);
    const QString keys = m_hotkey->registerFirst();
    connect(m_hotkey, &Hotkey::pressed, this, &Wormhole::toggleSearch);
    menu->addAction(keys.isEmpty() ? tr("Find in Notes...") : tr("Find in Notes...\t%1").arg(keys), this,
                    &Wormhole::toggleSearch);
    m_showDesk = menu->addAction(tr("Show Desk Notes"));
    m_showDesk->setCheckable(true);
    connect(m_showDesk, &QAction::triggered, this, &Wormhole::showDeskNotes);
    menu->addAction(tr("Export Notes..."), this, &Wormhole::exportNotes);
    m_hideAction = menu->addAction(tr("Hide Wormhole"));
    m_hideAction->setCheckable(true);
    connect(m_hideAction, &QAction::triggered, this, &Wormhole::setHidden);
    connect(menu, &QMenu::aboutToShow, this, [this] {
        m_showDesk->setEnabled(!m_desk.isEmpty() && !m_hidden);
        m_showDesk->setChecked(m_deskShown);
        m_hideAction->setChecked(m_hidden);
    });
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
    delete m_target;
    delete m_search;
    delete m_tray->contextMenu();
    delete m_note;
    delete m_hole;
}

void Wormhole::start()
{
    m_store.load();
    // A dated copy of the notes each day, so a bad edit or a lost file can be
    // got back. Checked hourly for the machine that is never restarted.
    m_store.backupDaily();
    auto *daily = new QTimer(this);
    daily->setInterval(60 * 60 * 1000);
    connect(daily, &QTimer::timeout, this, [this] { m_store.backupDaily(); });
    daily->start();
    // Hidden stays hidden across a restart: a recording should not be
    // interrupted by the hole coming back at sign-in.
    m_hidden = QSettings().value(QStringLiteral("hidden"), false).toBool();
    if (m_hidden)
        m_tray->setToolTip(tr("WormholeNotes (hidden)"));
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
    // Moving on to another app puts shown desk notes back with the desktop.
    showDeskNotes(false);
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
    if (m_hidden) {
        m_hole->hide();
        return;
    }
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

void Wormhole::view(const QString &key, const QString &label, int sheet)
{
    m_viewKey = key;
    m_viewLabel = label;
    showSheet(sheet);
    updateRing();
}

void Wormhole::showSheet(int index)
{
    const PlaceRecord record = m_store.place(m_viewKey);
    m_sheet = qBound(0, index, int(record.sheets.size()) - 1);
    m_lastSheet.insert(m_viewKey, m_sheet);
    m_note->setSheet(m_viewLabel, record.sheets.at(m_sheet), m_sheet, int(record.sheets.size()));
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
    showSheet(target);
}

void Wormhole::newSheet()
{
    const PlaceRecord record = m_store.place(m_viewKey);
    // A blank last sheet already is the new sheet.
    if (record.sheets.constLast().trimmed().isEmpty()) {
        showSheet(int(record.sheets.size()) - 1);
        return;
    }
    leaveSheet();
    showSheet(m_store.addSheet(m_viewKey, m_viewLabel));
}

void Wormhole::deleteSheet()
{
    m_store.removeSheet(m_viewKey, m_sheet);
    showSheet(qMin(m_sheet, int(m_store.place(m_viewKey).sheets.size()) - 1));
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
    desk->setRaised(m_deskShown);
    connect(desk, &DeskNote::clicked, this, [this, key] { openDeskNote(key); });
    connect(desk, &DeskNote::dropped, this, [this, key] { onDeskNoteDropped(key); });
    m_desk.insert(key, desk);
    if (!m_hidden)
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
    if (!m_hidden)
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

void Wormhole::exportNotes()
{
    putNoteAway();
    m_store.flush();
    const QString suggested = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        + QStringLiteral("/WormholeNotes-%1.md").arg(QDate::currentDate().toString(Qt::ISODate));
    const QString path = QFileDialog::getSaveFileName(nullptr, tr("Export Notes"), suggested, tr("Markdown (*.md)"));
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(m_store.exportMarkdown([this](const QString &key) { return placeLabel(key); }).toUtf8());
        if (file.commit()) {
            m_tray->showMessage(tr("Notes exported"), QDir::toNativeSeparators(path), QSystemTrayIcon::NoIcon, 4000);
            return;
        }
    }
    m_tray->showMessage(tr("Could not export"), file.errorString(), QSystemTrayIcon::Warning, 6000);
}

void Wormhole::toggleSearch()
{
    if (m_search->isVisible()) {
        m_search->hide();
        return;
    }
    putNoteAway();
    const QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    m_search->openAt((screen ? screen : QGuiApplication::primaryScreen())->availableGeometry().center());
}

QString Wormhole::placeLabel(const QString &key) const
{
    if (isDesk(key))
        return deskLabel(key);
    for (const Place &place : m_tracker.openPlaces()) {
        if (place.key == key)
            return place.label;
    }
    const QString stored = m_store.place(key).label;
    return stored.isEmpty() ? key.section(u'|', -1) : stored;
}

void Wormhole::runSearch(const QString &query)
{
    const QString needle = query.trimmed();
    QList<SearchHit> hits;
    if (needle.isEmpty()) {
        m_search->setHits(hits);
        return;
    }
    // The place in front first, then the rest by name.
    QStringList keys = m_store.keys();
    std::sort(keys.begin(), keys.end(), [this](const QString &a, const QString &b) {
        if ((a == m_place.key) != (b == m_place.key))
            return a == m_place.key;
        return placeLabel(a).compare(placeLabel(b), Qt::CaseInsensitive) < 0;
    });
    for (const QString &key : std::as_const(keys)) {
        const PlaceRecord record = m_store.place(key);
        const QString label = placeLabel(key);
        for (int sheet = 0; sheet < record.sheets.size(); ++sheet) {
            const QString &text = record.sheets.at(sheet);
            int perSheet = 0;
            for (qsizetype at = text.indexOf(needle, 0, Qt::CaseInsensitive); at >= 0 && perSheet < 3;
                 at = text.indexOf(needle, at + needle.size(), Qt::CaseInsensitive), ++perSheet) {
                SearchHit hit;
                hit.key = key;
                hit.label = label;
                hit.sheet = sheet;
                hit.start = int(at);
                hit.length = int(needle.size());
                const qsizetype from = qMax<qsizetype>(0, at - 60);
                hit.before = text.mid(from, at - from).replace(u'\n', u' ');
                hit.match = text.mid(at, needle.size());
                hit.after = text.mid(at + needle.size(), 60).replace(u'\n', u' ');
                hit.colour = Theme::sheetColour(record.colours.value(sheet));
                hits.append(hit);
            }
        }
    }
    m_search->setHits(hits);
}

void Wormhole::openHit(const SearchHit &hit)
{
    const QPoint center = m_search->geometry().center();
    if (isDesk(hit.key) && m_desk.contains(hit.key)) {
        m_lastSheet.insert(hit.key, hit.sheet);
        openDeskNote(hit.key);
    } else {
        putNoteAway();
        view(hit.key, placeLabel(hit.key), hit.sheet);
        m_note->openAt(center);
    }
    m_note->selectRange(hit.start, hit.length);
}

void Wormhole::setHidden(bool hidden)
{
    m_hidden = hidden;
    QSettings().setValue(QStringLiteral("hidden"), hidden);
    m_tray->setToolTip(hidden ? tr("WormholeNotes (hidden)") : QStringLiteral("WormholeNotes"));
    if (hidden) {
        putNoteAway();
        showDeskNotes(false);
        m_target->hide();
        for (DeskNote *desk : std::as_const(m_desk))
            desk->hide();
    } else {
        for (DeskNote *desk : std::as_const(m_desk))
            desk->show();
    }
    reposition();
}

void Wormhole::showDeskNotes(bool shown)
{
    if (shown == m_deskShown)
        return;
    m_deskShown = shown;
    for (DeskNote *desk : std::as_const(m_desk))
        desk->setRaised(shown);
}

void Wormhole::tearOff(const QPoint &where)
{
    if (isDesk(m_viewKey))
        return;
    m_store.setSheet(m_viewKey, m_viewLabel, m_sheet, m_note->text());
    tearOffSheet(m_viewKey, m_sheet, where.isNull() ? m_note->geometry().center() : where);
}

void Wormhole::tearOffSheet(const QString &key, int index, QPoint at)
{
    const PlaceRecord record = m_store.place(key);
    if (index < 0 || index >= record.sheets.size() || record.sheets.at(index).trimmed().isEmpty())
        return;
    const QString text = record.sheets.at(index);
    // Not on top of another desk note: the first free spot along a spiral out
    // from where it was dropped, kept on the screen.
    const QScreen *screen = QGuiApplication::screenAt(at);
    const QRect area = (screen ? screen : QGuiApplication::primaryScreen())->availableGeometry().adjusted(60, 60, -60, -60);
    const QPoint origin = at;
    for (int step = 0; step < 200; ++step) {
        const qreal angle = step * 0.9;
        const qreal reach = 22.0 * std::sqrt(qreal(step)) * 5;
        const QPoint candidate(qBound(area.left(), qRound(origin.x() + reach * std::cos(angle)), area.right()),
                               qBound(area.top(), qRound(origin.y() + reach * std::sin(angle)), area.bottom()));
        bool free = true;
        for (const DeskNote *desk : std::as_const(m_desk)) {
            if (QLineF(desk->logicalCenter(), candidate).length() < 118) {
                free = false;
                break;
            }
        }
        if (free) {
            at = candidate;
            break;
        }
    }
    const QString desk = m_store.newDeskNote({ text }, at, { record.colours.value(index) });
    // The sheet leaves the page, which is left with a fresh blank if that
    // was its only one.
    m_store.removeSheet(key, index);
    const int remaining = int(m_store.place(key).sheets.size());
    m_lastSheet.insert(key, qMin(index, remaining - 1));
    if (m_note->isVisible() && m_viewKey == key)
        m_sheet = qMin(m_sheet, remaining - 1);
    createDeskNote(desk);
    putNoteAway();
    updateHole();
}

// A note dropped on the tear-off target, or where only the desktop shows, is
// torn off there. Moving it aside over another window is just moving it.
void Wormhole::onNoteMoved()
{
    const bool onTarget = m_target->track();
    const QPoint target = m_target->geometry().center();
    m_target->hide();
    if (!m_note->isVisible() || isDesk(m_viewKey))
        return;
    if (onTarget) {
        // Just above the target, so the new desk note is seen landing.
        tearOff(target - QPoint(0, 130));
        return;
    }
    if (m_tracker.windowAt(m_note->physicalCenter()) == 0)
        tearOff();
}

void Wormhole::onHoleDragged(const QPoint &c)
{
    const bool onTarget = m_target->track();
    const QPoint target = m_target->geometry().center();
    m_target->hide();
    if (onTarget) {
        // The hole goes home to its window; the sheet goes to the desktop.
        tearOffSheet(m_place.key, m_lastSheet.value(m_place.key, 0), target - QPoint(0, 130));
        reposition();
        return;
    }
    int dpi = 96;
    const QRect frame = m_tracker.anchorRect(&dpi);
    if (frame.isEmpty()) {
        reposition();
        return;
    }
    // The hole belongs on its window. Dropped off it, it comes back to the
    // nearest spot on it rather than hanging in the air beside it.
    const int margin = qRound(12 * dpi / 96.0);
    const QPoint inside(qBound(frame.left() + margin, c.x(), frame.right() - margin),
                        qBound(frame.top() + margin, c.y(), frame.bottom() - margin));
    const qreal scale = dpi / 96.0;
    m_store.setHole(m_place.key,
                    QPointF((frame.right() - inside.x()) / scale, (inside.y() - frame.top()) / scale));
    reposition();
}

void Wormhole::quit()
{
    putNoteAway();
    m_store.flush();
    QApplication::quit();
}
