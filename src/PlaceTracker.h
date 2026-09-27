#pragma once

#include "NativeHost.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QRect>
#include <QSet>
#include <QString>
#include <QThread>
#include <QTimer>

class BrowserReader;

// Where the user is working. Every app is one place; a browser is one place
// per page, since the page is what the note is about.
struct Place
{
    QString key;   // stable identity, what sheets are filed under
    QString label; // for people: the app name, or the page's host
    quintptr hwnd = 0; // the top-level window the hole rides on; 0 for the desktop

    bool isDesktop() const { return hwnd == 0; }
    bool operator==(const Place &other) const { return key == other.key && hwnd == other.hwnd; }
};

// Follows the foreground window through WinEvents and reports the place it
// belongs to, plus every move of that window so the hole can ride along. It
// also keeps the list of places that are open right now, which is what the
// ring on the note's bezel shows.
//
// Page detection is layered: the browser extension where it is installed,
// then the address bar through UI Automation, then the window title.
class PlaceTracker : public QObject
{
    Q_OBJECT

public:
    explicit PlaceTracker(QObject *parent = nullptr);
    ~PlaceTracker() override;

    void start();
    Place current() const { return m_place; }

    // Open places in ring order: the desktop first, then taskbar order, with
    // a browser's pages together where its button is. Anything not on the
    // taskbar follows, in the order it appeared.
    QList<Place> openPlaces() const;

    // The top-level window that shows at a point on screen, in physical
    // pixels, looking through our own windows. 0 when it is the desktop.
    quintptr windowAt(const QPoint &physical) const;
    // The place a window belongs to. False for windows that are not a place.
    bool placeOf(quintptr hwnd, Place *place) const;

    // A report from the browser extension, which outranks every other way of
    // knowing a browser's pages.
    void onExtensionReport(const ExtensionReport &report);

    // The tracked window's frame on screen in physical pixels, and its DPI.
    // Empty when the window is minimized or gone, which hides the hole.
    QRect anchorRect(int *dpi = nullptr) const;

Q_SIGNALS:
    void placeChanged(const Place &place);
    void anchorMoved();
    void openPlacesChanged();
    // The page or app is gone; its blank sheets go with it.
    void placeClosed(const QString &key);

    void requestAddress(quintptr hwnd);
    void requestTabs(QList<quintptr> hwnds);
    void requestTaskbar();

private:
    friend struct PlaceTrackerHooks;

    struct Window
    {
        quintptr hwnd = 0;
        QString exe;      // lower-case file name
        QString appName;  // file name without extension, as it was
        QString title;
        qint64 created = 0; // process start, for first-seen ordering
        bool desktop = false;
        bool browser = false;
    };

    struct OpenPlace
    {
        Place place;
        qint64 order = 0;
        bool browser = false;
        // Apps only: the windows it has, for finding it on the taskbar.
        QSet<quintptr> windows;
        // Browser pages only: which windows have it, and under which tab
        // titles it was seen, so a closed tab can be told from a live one.
        QHash<quintptr, QSet<QString>> titles;
    };

    void onForeground(quintptr hwnd);
    void onLocation(quintptr hwnd);
    void onName(quintptr hwnd);
    void resolve();
    void setPlace(const Place &place, const QString &pageTitle);
    bool describe(quintptr hwnd, Window *window, bool forRing) const;
    Place appPlace(const Window &window) const;
    Place browserPlace(const Window &window, const QString &page) const;

    void onAddressRead(quintptr hwnd, const QString &page);
    void onTabsRead(quintptr hwnd, const QStringList &titles, const QString &page);
    void onTaskbarRead(const QList<quintptr> &windows);
    int taskbarPosition(const OpenPlace &open) const;
    void addOpen(const Place &place, const QString &pageTitle);
    void refreshOpen();
    qint64 rankFor(const QString &exe, qint64 created);
    void close(const QString &key);

    Place m_place;
    QTimer m_settle;
    QTimer m_poll;
    int m_pollCount = 0;
    quintptr m_foregroundHook = 0;
    quintptr m_objectHook = 0;

    QThread m_readerThread;
    BrowserReader *m_reader = nullptr;
    // The last address-bar read per browser window, and the title the window
    // had when it was asked. A new title means the page may have changed.
    QHash<quintptr, QString> m_pageOf;
    QHash<quintptr, QString> m_readTitle;
    quintptr m_pendingHwnd = 0;
    QString m_pendingTitle;

    struct ExtensionState
    {
        QString browser;
        QList<ExtensionReport::Page> showing;
        QList<ExtensionReport::Page> tabs;
        // The places this copy of the extension opened, and the windows it
        // found for them.
        QSet<QString> keys;
        QSet<quintptr> hosts;
    };
    // By source: one per browser profile running the extension.
    QHash<qint64, ExtensionState> m_extension;
    bool hasExtension(const QString &exe) const;

    QHash<QString, OpenPlace> m_open;
    QHash<QString, qint64> m_rank; // by exe
    QHash<quintptr, int> m_taskbar; // window to its button's position
    qint64 m_nextRank = 0;
    bool m_ranked = false;
    QList<quintptr> m_scratch;
};
