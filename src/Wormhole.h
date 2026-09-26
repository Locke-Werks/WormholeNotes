#pragma once

#include "PlaceTracker.h"
#include "SheetStore.h"

#include <QHash>
#include <QObject>

class DeskNote;
class HoleWindow;
class NoteWindow;
class QSystemTrayIcon;

// Ties the pieces together: the tracker says where the user is, the store
// holds that place's sheets, the hole rides the window, and the note opens
// out of the hole.
//
// The hole always belongs to the place in the foreground. The note starts
// there, but the clip can turn it to any other open place without leaving
// the app the user is in.
class Wormhole : public QObject
{
    Q_OBJECT

public:
    explicit Wormhole(QObject *parent = nullptr);
    ~Wormhole() override;

    void start();

private:
    void onPlaceChanged(const Place &place);
    void onPlaceClosed(const QString &key);
    void reposition();
    void openNote();
    void putNoteAway();
    void onHoleDragged(const QPoint &physicalCenter);
    void quit();

    // The place the note is showing, and which of its sheets.
    void view(const QString &key, const QString &label, int sheet, bool fromEnd = false);
    void showSheet(int index, bool fromEnd);
    void turnSheet(int direction);
    void newSheet();
    void deleteSheet();
    void turnPlace(const QString &key);
    // A blank sheet left behind among others is dropped, so turning past the
    // end to look does not leave empty sheets lying about.
    void leaveSheet();
    void updateRing();
    void updateHole();

    // Notes torn off onto the desktop.
    bool isDesk(const QString &key) const;
    QString deskLabel(const QString &key) const;
    void createDeskNote(const QString &key);
    void openDeskNote(const QString &key);
    // A desk note that is no longer being looked at goes back on the desk,
    // or away altogether when nothing is left written on it.
    void settleDesk(const QString &key);
    void onDeskNoteDropped(const QString &key);
    void tearOff();
    void onNoteMoved();

    PlaceTracker m_tracker;
    SheetStore m_store;
    Place m_place;
    QString m_viewKey;
    QString m_viewLabel;
    int m_sheet = 0;
    QHash<QString, int> m_lastSheet;
    QHash<QString, DeskNote *> m_desk;
    HoleWindow *m_hole = nullptr;
    NoteWindow *m_note = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
};
