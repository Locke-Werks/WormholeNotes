#pragma once

#include "PlaceTracker.h"
#include "SheetStore.h"

#include <QObject>

class HoleWindow;
class NoteWindow;
class QSystemTrayIcon;

// Ties the pieces together: the tracker says where the user is, the store
// holds that place's sheet, the hole rides the window, and the note opens
// out of the hole.
class Wormhole : public QObject
{
    Q_OBJECT

public:
    explicit Wormhole(QObject *parent = nullptr);
    ~Wormhole() override;

    void start();

private:
    void onPlaceChanged(const Place &place);
    void reposition();
    void openNote();
    void putNoteAway();
    void onHoleDragged(const QPoint &physicalCenter);
    void quit();

    PlaceTracker m_tracker;
    SheetStore m_store;
    Place m_place;
    HoleWindow *m_hole = nullptr;
    NoteWindow *m_note = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
};
