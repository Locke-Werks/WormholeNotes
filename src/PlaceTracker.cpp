#include "PlaceTracker.h"

#include "BrowserReader.h"

#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <climits>
#include <utility>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>

namespace {

PlaceTracker *g_tracker = nullptr;

const QStringList kBrowsers = {
    QStringLiteral("chrome.exe"), QStringLiteral("msedge.exe"),  QStringLiteral("firefox.exe"),
    QStringLiteral("brave.exe"),  QStringLiteral("opera.exe"),   QStringLiteral("vivaldi.exe"),
    QStringLiteral("arc.exe"),    QStringLiteral("librewolf.exe"),
};

// Shell surfaces that take the foreground for a moment on the way to
// somewhere else: the taskbar, Start, Alt+Tab, the notification center.
// Following them would move the hole every time the user reached for a window.
const QStringList kTransientClasses = {
    QStringLiteral("Shell_TrayWnd"),
    QStringLiteral("Shell_SecondaryTrayWnd"),
    QStringLiteral("Windows.UI.Core.CoreWindow"),
    QStringLiteral("XamlExplorerHostIslandWindow"),
    QStringLiteral("ForegroundStaging"),
    QStringLiteral("MultitaskingViewFrame"),
    QStringLiteral("TaskListThumbnailWnd"),
    QStringLiteral("NotifyIconOverflowWindow"),
    QStringLiteral("TopLevelWindowForOverflowXamlIsland"),
};

const QString kDesktopKey = QStringLiteral("desktop");

QString windowClass(HWND hwnd)
{
    wchar_t buffer[256] = {};
    const int n = GetClassNameW(hwnd, buffer, 256);
    return QString::fromWCharArray(buffer, n);
}

QString windowTitle(HWND hwnd)
{
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0)
        return {};
    std::wstring buffer(size_t(length) + 1, L'\0');
    const int n = GetWindowTextW(hwnd, buffer.data(), length + 1);
    return QString::fromWCharArray(buffer.data(), n);
}

QString processPath(DWORD pid, qint64 *created)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};
    wchar_t buffer[MAX_PATH * 2] = {};
    DWORD size = DWORD(std::size(buffer));
    QString path;
    if (QueryFullProcessImageNameW(process, 0, buffer, &size))
        path = QString::fromWCharArray(buffer, int(size));
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (created && GetProcessTimes(process, &creation, &exit, &kernel, &user))
        *created = qint64((quint64(creation.dwHighDateTime) << 32) | creation.dwLowDateTime);
    CloseHandle(process);
    return path;
}

// "Page - Google Chrome", "Page — Mozilla Firefox", "Page and 3 more pages -
// Personal - Microsoft Edge". The page is everything before the browser's own
// name. It is also what the tab strip calls the tab.
QString pageFromTitle(QString title)
{
    static const QRegularExpression separator(QStringLiteral("\\s+[-\u2014\u2013]\\s+"));
    static const QRegularExpression morePages(QStringLiteral("\\s+and \\d+ more pages?$"));
    QStringList parts = title.split(separator);
    if (parts.size() > 1)
        parts.removeLast();
    // Edge puts the profile between the page and its own name.
    if (parts.size() > 1 && title.contains(QStringLiteral("Edge")))
        parts.removeLast();
    title = parts.join(QStringLiteral(" - ")).trimmed();
    title.remove(morePages);
    return title;
}

// "notes.txt - Notepad" is Notepad. Most apps end their title with their own
// name, which is what a place is called on the ring.
QString appLabel(const QString &title, const QString &appName)
{
    static const QRegularExpression separator(QStringLiteral("\\s+[-\u2014\u2013|]\\s+"));
    const QStringList parts = title.split(separator, Qt::SkipEmptyParts);
    if (!parts.isEmpty())
        return parts.last().trimmed();
    return appName;
}

bool sameTab(const QString &tab, const QString &seen)
{
    return !tab.isEmpty() && !seen.isEmpty() && (tab.startsWith(seen) || seen.startsWith(tab));
}

} // namespace

struct PlaceTrackerHooks
{
    static void CALLBACK onEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD,
                                 DWORD)
    {
        if (!g_tracker || idObject != OBJID_WINDOW || idChild != CHILDID_SELF)
            return;
        const quintptr h = quintptr(hwnd);
        switch (event) {
        case EVENT_SYSTEM_FOREGROUND:
            g_tracker->onForeground(h);
            break;
        case EVENT_OBJECT_LOCATIONCHANGE:
            g_tracker->onLocation(h);
            break;
        case EVENT_OBJECT_NAMECHANGE:
            g_tracker->onName(h);
            break;
        default:
            break;
        }
    }
};

PlaceTracker::PlaceTracker(QObject *parent)
    : QObject(parent)
{
    g_tracker = this;
    m_settle.setSingleShot(true);
    m_settle.setInterval(60);
    connect(&m_settle, &QTimer::timeout, this, &PlaceTracker::resolve);
    // A slow check for anything the events missed, such as a refused
    // activation that Windows grants later without raising a second event,
    // and the walk that finds which places are still open.
    m_poll.setInterval(750);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        resolve();
        refreshOpen();
    });

    OpenPlace desktop;
    desktop.place.key = kDesktopKey;
    desktop.place.label = tr("Desktop");
    desktop.order = -1;
    m_open.insert(kDesktopKey, desktop);

    m_reader = new BrowserReader;
    m_reader->moveToThread(&m_readerThread);
    connect(&m_readerThread, &QThread::finished, m_reader, &QObject::deleteLater);
    connect(this, &PlaceTracker::requestAddress, m_reader, &BrowserReader::readAddress);
    connect(this, &PlaceTracker::requestTabs, m_reader, &BrowserReader::readTabs);
    connect(m_reader, &BrowserReader::addressRead, this, &PlaceTracker::onAddressRead);
    connect(m_reader, &BrowserReader::tabsRead, this, &PlaceTracker::onTabsRead);
    connect(this, &PlaceTracker::requestTaskbar, m_reader, &BrowserReader::readTaskbar);
    connect(m_reader, &BrowserReader::taskbarRead, this, &PlaceTracker::onTaskbarRead);
    m_readerThread.start();
}

PlaceTracker::~PlaceTracker()
{
    if (m_foregroundHook)
        UnhookWinEvent(HWINEVENTHOOK(m_foregroundHook));
    if (m_objectHook)
        UnhookWinEvent(HWINEVENTHOOK(m_objectHook));
    g_tracker = nullptr;
    m_readerThread.quit();
    m_readerThread.wait();
}

void PlaceTracker::start()
{
    // Out of context, so nothing is injected into other processes; the
    // callbacks arrive on this thread through its message loop.
    const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    m_foregroundHook = quintptr(SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                                                &PlaceTrackerHooks::onEvent, 0, 0, flags));
    // LOCATIONCHANGE and NAMECHANGE are adjacent, so one hook takes both.
    m_objectHook = quintptr(SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_NAMECHANGE, nullptr,
                                            &PlaceTrackerHooks::onEvent, 0, 0, flags));
    // Ranks come from the first walk, so it runs before any place is set.
    refreshOpen();
    resolve();
    m_poll.start();
}

bool PlaceTracker::describe(quintptr h, Window *w, bool forRing) const
{
    HWND hwnd = HWND(h);
    if (!hwnd || !IsWindow(hwnd))
        return false;
    hwnd = GetAncestor(hwnd, GA_ROOT);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId())
        return false;

    const QString cls = windowClass(hwnd);
    if (cls == QLatin1String("Progman") || cls == QLatin1String("WorkerW")) {
        w->desktop = true;
        return !forRing;
    }
    if (kTransientClasses.contains(cls) || !IsWindowVisible(hwnd))
        return false;

    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof cloaked);
    if (cloaked)
        return false;

    w->title = windowTitle(hwnd);
    if (forRing) {
        // What Alt+Tab would show: an unowned window, or one that asks for a
        // taskbar button, never a tool window, and with a title.
        const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        const bool owned = GetWindow(hwnd, GW_OWNER) != nullptr;
        if ((ex & WS_EX_TOOLWINDOW) || (owned && !(ex & WS_EX_APPWINDOW)) || w->title.isEmpty())
            return false;
    }

    const QString path = processPath(pid, &w->created);
    w->exe = QFileInfo(path).fileName().toLower();
    if (w->exe.isEmpty())
        return false;
    w->appName = QFileInfo(path).completeBaseName();
    w->hwnd = quintptr(hwnd);
    w->browser = kBrowsers.contains(w->exe);
    return true;
}

Place PlaceTracker::appPlace(const Window &w) const
{
    Place place;
    place.hwnd = w.hwnd;
    if (w.exe == QLatin1String("applicationframehost.exe")) {
        // Store apps all run under one host; the title is what tells them apart.
        place.key = w.exe + u'|' + w.title;
        place.label = w.title;
    } else {
        place.key = w.exe;
        place.label = appLabel(w.title, w.appName);
    }
    return place;
}

Place PlaceTracker::browserPlace(const Window &w, const QString &page) const
{
    Place place;
    place.hwnd = w.hwnd;
    if (!page.isEmpty()) {
        place.key = w.exe + u'|' + page;
        place.label = page;
    } else {
        // The address bar could not be read, so the title stands in, the
        // least stable of the layers since it changes as a page loads.
        const QString title = pageFromTitle(w.title);
        place.key = w.exe + QStringLiteral("|title|") + title;
        place.label = title.isEmpty() ? w.appName : title;
    }
    return place;
}

void PlaceTracker::onForeground(quintptr)
{
    // The event names the window that asked for the foreground, which is not
    // always the one that got it: a launch Windows refuses to activate still
    // raises it, and the window is often not visible yet. So the event only
    // says "look again soon", and GetForegroundWindow is the answer.
    m_settle.start();
}

void PlaceTracker::resolve()
{
    Window w;
    if (!describe(quintptr(GetForegroundWindow()), &w, false))
        return;
    if (w.desktop) {
        setPlace(m_open.value(kDesktopKey).place, {});
        return;
    }
    if (!w.browser) {
        setPlace(appPlace(w), {});
        return;
    }
    // With the extension, the browser says which page each window shows.
    if (auto ext = m_extension.constFind(w.exe); ext != m_extension.constEnd()) {
        const QString title = pageFromTitle(w.title);
        for (const ExtensionReport::Page &page : ext->showing) {
            if (!page.page.isEmpty() && sameTab(page.title, title)) {
                setPlace(browserPlace(w, page.page), title);
                return;
            }
        }
    }
    // Otherwise a browser's place is its page, which takes a read of the
    // address bar.
    // The read is asked for again only when the title changes, since that is
    // what a new page or a different tab looks like from outside.
    if (m_readTitle.contains(w.hwnd) && m_readTitle.value(w.hwnd) == w.title) {
        setPlace(browserPlace(w, m_pageOf.value(w.hwnd)), pageFromTitle(w.title));
        return;
    }
    if (m_pendingHwnd == w.hwnd && m_pendingTitle == w.title)
        return;
    m_pendingHwnd = w.hwnd;
    m_pendingTitle = w.title;
    emit requestAddress(w.hwnd);
}

void PlaceTracker::onAddressRead(quintptr hwnd, const QString &page)
{
    if (hwnd != m_pendingHwnd)
        return;
    m_pageOf.insert(hwnd, page);
    m_readTitle.insert(hwnd, m_pendingTitle);
    m_pendingHwnd = 0;
    m_pendingTitle.clear();
    resolve();
}

void PlaceTracker::setPlace(const Place &place, const QString &pageTitle)
{
    addOpen(place, pageTitle);
    if (place == m_place) {
        m_place.label = place.label;
        return;
    }
    m_place = place;
    emit placeChanged(m_place);
}

void PlaceTracker::addOpen(const Place &place, const QString &pageTitle)
{
    auto it = m_open.find(place.key);
    const bool added = it == m_open.end();
    if (added) {
        OpenPlace open;
        open.place = place;
        if (place.key != kDesktopKey) {
            Window w;
            describe(place.hwnd, &w, false);
            open.order = rankFor(w.exe, w.created) * 1000;
            open.browser = w.browser;
            if (w.browser) {
                // Pages follow their browser, in the order they were visited.
                qint64 last = open.order;
                for (const OpenPlace &other : std::as_const(m_open)) {
                    if (other.order > last && other.order < open.order + 1000)
                        last = other.order;
                }
                open.order = last + 1;
            }
        }
        it = m_open.insert(place.key, open);
    }
    it->place.label = place.label;
    it->place.hwnd = place.hwnd;
    if (!pageTitle.isEmpty())
        it->titles[place.hwnd].insert(pageTitle);
    if (added)
        emit openPlacesChanged();
}

qint64 PlaceTracker::rankFor(const QString &exe, qint64)
{
    if (auto it = m_rank.constFind(exe); it != m_rank.constEnd())
        return *it;
    const qint64 rank = ++m_nextRank;
    m_rank.insert(exe, rank);
    return rank;
}

void PlaceTracker::refreshOpen()
{
    QList<quintptr> handles;
    EnumWindows(
        [](HWND hwnd, LPARAM data) -> BOOL {
            reinterpret_cast<QList<quintptr> *>(data)->append(quintptr(hwnd));
            return TRUE;
        },
        LPARAM(&handles));

    QList<Window> windows;
    for (const quintptr h : handles) {
        Window w;
        if (describe(h, &w, true))
            windows.append(w);
    }

    // The first walk ranks what is already running by when it started, the
    // closest outside view of taskbar order. Anything later joins the end,
    // which is where the taskbar puts it too.
    if (!m_ranked) {
        QList<Window> byStart = windows;
        std::stable_sort(byStart.begin(), byStart.end(),
                         [](const Window &a, const Window &b) { return a.created < b.created; });
        for (const Window &w : byStart)
            rankFor(w.exe, w.created);
        m_ranked = true;
    }

    QSet<QString> liveApps;
    QSet<quintptr> liveBrowsers;
    QList<quintptr> browserWindows;
    bool changed = false;
    for (const Window &w : std::as_const(windows)) {
        if (w.browser) {
            liveBrowsers.insert(w.hwnd);
            browserWindows.append(w.hwnd);
            rankFor(w.exe, w.created);
            continue;
        }
        const Place place = appPlace(w);
        if (!liveApps.contains(place.key)) {
            liveApps.insert(place.key);
            if (auto it = m_open.find(place.key); it != m_open.end())
                it->windows.clear();
        }
        if (!m_open.contains(place.key)) {
            OpenPlace open;
            open.place = place;
            open.order = rankFor(w.exe, w.created) * 1000;
            m_open.insert(place.key, open);
            changed = true;
        }
        m_open[place.key].windows.insert(w.hwnd);
    }

    for (auto it = m_pageOf.begin(); it != m_pageOf.end();) {
        if (!IsWindow(HWND(it.key()))) {
            m_readTitle.remove(it.key());
            it = m_pageOf.erase(it);
        } else {
            ++it;
        }
    }

    QStringList closed;
    for (auto it = m_open.begin(); it != m_open.end(); ++it) {
        if (it.key() == kDesktopKey)
            continue;
        if (!it->browser) {
            if (!liveApps.contains(it.key()) && it.key() != m_place.key)
                closed.append(it.key());
            continue;
        }
        for (auto t = it->titles.begin(); t != it->titles.end();) {
            if (!liveBrowsers.contains(t.key()))
                t = it->titles.erase(t);
            else
                ++t;
        }
        if (it->titles.isEmpty() && it.key() != m_place.key)
            closed.append(it.key());
    }
    for (const QString &key : std::as_const(closed))
        close(key);
    if (changed && closed.isEmpty())
        emit openPlacesChanged();

    // The tab strip and the taskbar are longer walks than the address bar,
    // so they are read less often, and the taskbar at once when something
    // opened.
    const bool slowTurn = ++m_pollCount % 4 == 0;
    if (slowTurn && !browserWindows.isEmpty())
        emit requestTabs(browserWindows);
    if (slowTurn || changed || m_pollCount == 1)
        emit requestTaskbar();
}

void PlaceTracker::onTabsRead(quintptr hwnd, const QStringList &tabs, const QString &page)
{
    // The extension's list of tabs is exact; the tab strip is only a guess.
    Window browser;
    if (describe(hwnd, &browser, false) && m_extension.contains(browser.exe))
        return;
    // Every browser window's active page is open, visited or not.
    Window w;
    if (!page.isEmpty() && describe(hwnd, &w, false) && w.browser)
        addOpen(browserPlace(w, page), pageFromTitle(w.title));
    if (tabs.isEmpty())
        return;
    QStringList closed;
    for (auto it = m_open.begin(); it != m_open.end(); ++it) {
        auto seen = it->titles.find(hwnd);
        if (seen == it->titles.end())
            continue;
        // The page on screen is open whatever its tab is called by now.
        if (it.key() == m_place.key && hwnd == m_place.hwnd)
            continue;
        bool open = false;
        for (const QString &tab : tabs) {
            for (const QString &title : std::as_const(*seen)) {
                if (sameTab(tab, title)) {
                    open = true;
                    break;
                }
            }
            if (open)
                break;
        }
        if (!open) {
            it->titles.erase(seen);
            if (it->titles.isEmpty())
                closed.append(it.key());
        }
    }
    for (const QString &key : std::as_const(closed))
        close(key);
}

void PlaceTracker::onExtensionReport(const ExtensionReport &report)
{
    m_extension.insert(report.browser, { report.showing, report.tabs });

    QList<Window> windows;
    EnumWindows(
        [](HWND hwnd, LPARAM data) -> BOOL {
            reinterpret_cast<QList<quintptr> *>(data)->append(quintptr(hwnd));
            return TRUE;
        },
        LPARAM(&m_scratch));
    for (const quintptr h : std::exchange(m_scratch, {})) {
        Window w;
        if (describe(h, &w, true) && w.exe == report.browser)
            windows.append(w);
    }
    if (windows.isEmpty())
        return;

    // Every open tab is an open place. A tab showing in a window rides that
    // window; one behind another tab rides the first window, which is only
    // used for its place on the taskbar.
    QSet<QString> open;
    for (const ExtensionReport::Page &tab : report.tabs) {
        if (tab.page.isEmpty())
            continue;
        const Window *host = &windows.constFirst();
        for (const Window &w : std::as_const(windows)) {
            if (sameTab(tab.title, pageFromTitle(w.title)))
                host = &w;
        }
        const Place place = browserPlace(*host, tab.page);
        open.insert(place.key);
        addOpen(place, tab.title);
    }

    const QString prefix = report.browser + u'|';
    QStringList closed;
    for (auto it = m_open.cbegin(); it != m_open.cend(); ++it) {
        if (it.key().startsWith(prefix) && !open.contains(it.key()) && it.key() != m_place.key)
            closed.append(it.key());
    }
    for (const QString &key : std::as_const(closed))
        close(key);
    m_settle.start();
}

void PlaceTracker::onTaskbarRead(const QList<quintptr> &windows)
{
    QHash<quintptr, int> taskbar;
    for (int i = 0; i < windows.size(); ++i)
        taskbar.insert(windows.at(i), i);
    if (taskbar == m_taskbar)
        return;
    m_taskbar = taskbar;
    emit openPlacesChanged();
}

int PlaceTracker::taskbarPosition(const OpenPlace &open) const
{
    int best = INT_MAX;
    const auto consider = [&](quintptr hwnd) {
        if (auto it = m_taskbar.constFind(hwnd); it != m_taskbar.constEnd())
            best = qMin(best, *it);
    };
    for (const quintptr hwnd : open.windows)
        consider(hwnd);
    for (auto it = open.titles.cbegin(); it != open.titles.cend(); ++it)
        consider(it.key());
    return best;
}

void PlaceTracker::close(const QString &key)
{
    if (!m_open.remove(key))
        return;
    emit placeClosed(key);
    emit openPlacesChanged();
}

quintptr PlaceTracker::windowAt(const QPoint &physical) const
{
    struct Search
    {
        const PlaceTracker *tracker;
        POINT point;
        quintptr found;
    } search{ this, { physical.x(), physical.y() }, 0 };

    // Top to bottom through the stack, so the first window that contains the
    // point is the one showing there.
    EnumWindows(
        [](HWND hwnd, LPARAM data) -> BOOL {
            auto *s = reinterpret_cast<Search *>(data);
            if (!IsWindowVisible(hwnd) || IsIconic(hwnd))
                return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == GetCurrentProcessId())
                return TRUE;
            BOOL cloaked = FALSE;
            DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof cloaked);
            if (cloaked || (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT))
                return TRUE;
            RECT r{};
            if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof r)))
                GetWindowRect(hwnd, &r);
            if (!PtInRect(&r, s->point))
                return TRUE;
            const QString cls = windowClass(hwnd);
            if (cls == QLatin1String("Progman") || cls == QLatin1String("WorkerW")) {
                s->found = 0;
                return FALSE;
            }
            if (kTransientClasses.contains(cls))
                return TRUE;
            s->found = quintptr(hwnd);
            return FALSE;
        },
        LPARAM(&search));
    return search.found;
}

bool PlaceTracker::placeOf(quintptr hwnd, Place *place) const
{
    Window w;
    if (!describe(hwnd, &w, false) || w.desktop)
        return false;
    *place = w.browser ? browserPlace(w, m_pageOf.value(w.hwnd)) : appPlace(w);
    return true;
}

QList<Place> PlaceTracker::openPlaces() const
{
    struct Entry
    {
        const OpenPlace *open;
        int taskbar;
    };
    QList<Entry> sorted;
    for (const OpenPlace &open : m_open)
        sorted.append({ &open, open.place.key == kDesktopKey ? -1 : taskbarPosition(open) });
    std::sort(sorted.begin(), sorted.end(), [](const Entry &a, const Entry &b) {
        if (a.taskbar != b.taskbar)
            return a.taskbar < b.taskbar;
        if (a.open->order != b.open->order)
            return a.open->order < b.open->order;
        return a.open->place.key < b.open->place.key;
    });
    QList<Place> out;
    for (const Entry &entry : std::as_const(sorted))
        out.append(entry.open->place);
    return out;
}

void PlaceTracker::onLocation(quintptr hwnd)
{
    if (hwnd && hwnd == m_place.hwnd)
        emit anchorMoved();
}

void PlaceTracker::onName(quintptr hwnd)
{
    // A browser changes its title when the tab or the page changes, which is
    // the cue to read its address bar again.
    if (hwnd && hwnd == m_place.hwnd && HWND(hwnd) == GetForegroundWindow())
        m_settle.start();
}

QRect PlaceTracker::anchorRect(int *dpi) const
{
    if (m_place.isDesktop()) {
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        if (dpi)
            *dpi = int(GetDpiForSystem());
        return QRect(QPoint(work.left, work.top), QPoint(work.right - 1, work.bottom - 1));
    }
    HWND hwnd = HWND(m_place.hwnd);
    if (!IsWindow(hwnd) || IsIconic(hwnd) || !IsWindowVisible(hwnd))
        return {};
    RECT r{};
    // The visible frame, without the invisible resize borders Windows 10 and
    // later add around a window.
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof r)))
        GetWindowRect(hwnd, &r);
    if (dpi)
        *dpi = int(GetDpiForWindow(hwnd));
    return QRect(QPoint(r.left, r.top), QPoint(r.right - 1, r.bottom - 1));
}
