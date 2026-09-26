#pragma once

#include <QObject>
#include <QRect>
#include <QString>
#include <QTimer>

// Where the user is working. Every app is one place; a browser is one place
// per page, since the page is what the note is about.
struct Place
{
    QString key;   // stable identity, what a sheet is filed under
    QString label; // for people: the app or page name
    quintptr hwnd = 0; // the top-level window the hole rides on; 0 for the desktop

    bool isDesktop() const { return hwnd == 0; }
    bool operator==(const Place &other) const { return key == other.key && hwnd == other.hwnd; }
};

// Follows the foreground window through WinEvents and reports the place it
// belongs to, plus every move of that window so the hole can ride along.
//
// Page detection is layered: extension, then the address bar through UI
// Automation, then the window title. Only the title layer exists so far.
class PlaceTracker : public QObject
{
    Q_OBJECT

public:
    explicit PlaceTracker(QObject *parent = nullptr);
    ~PlaceTracker() override;

    void start();
    Place current() const { return m_place; }

    // The tracked window's frame on screen in physical pixels, and its DPI.
    // Empty when the window is minimized or gone, which hides the hole.
    QRect anchorRect(int *dpi = nullptr) const;

Q_SIGNALS:
    void placeChanged(const Place &place);
    void anchorMoved();

private:
    friend struct PlaceTrackerHooks;
    void onForeground(quintptr hwnd);
    void onLocation(quintptr hwnd);
    void onName(quintptr hwnd);
    void resolve();
    bool placeFor(quintptr hwnd, Place *place) const;

    Place m_place;
    QTimer m_settle;
    QTimer m_poll;
    quintptr m_foregroundHook = 0;
    quintptr m_objectHook = 0;
};
