#include "PlaceTracker.h"

#include <QFileInfo>
#include <QRegularExpression>

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

QString processPath(HWND hwnd, DWORD *pid)
{
    GetWindowThreadProcessId(hwnd, pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, *pid);
    if (!process)
        return {};
    wchar_t buffer[MAX_PATH * 2] = {};
    DWORD size = DWORD(std::size(buffer));
    QString path;
    if (QueryFullProcessImageNameW(process, 0, buffer, &size))
        path = QString::fromWCharArray(buffer, int(size));
    CloseHandle(process);
    return path;
}

// "Page - Google Chrome", "Page — Mozilla Firefox", "Page and 3 more pages -
// Personal - Microsoft Edge". The page is everything before the browser's own
// name. Titles change as pages load, so this layer is the least stable of the
// three and the first to be replaced when the address bar can be read.
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
    // activation that Windows grants later without raising a second event.
    m_poll.setInterval(750);
    connect(&m_poll, &QTimer::timeout, this, &PlaceTracker::resolve);
}

PlaceTracker::~PlaceTracker()
{
    if (m_foregroundHook)
        UnhookWinEvent(HWINEVENTHOOK(m_foregroundHook));
    if (m_objectHook)
        UnhookWinEvent(HWINEVENTHOOK(m_objectHook));
    g_tracker = nullptr;
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
    resolve();
    m_poll.start();
}

bool PlaceTracker::placeFor(quintptr h, Place *place) const
{
    HWND hwnd = HWND(h);
    if (!hwnd || !IsWindow(hwnd)) {
        return false;
    }
    hwnd = GetAncestor(hwnd, GA_ROOT);

    DWORD pid = 0;
    const QString path = processPath(hwnd, &pid);
    if (pid == GetCurrentProcessId())
        return false;

    const QString cls = windowClass(hwnd);
    if (cls == QLatin1String("Progman") || cls == QLatin1String("WorkerW")) {
        place->key = QStringLiteral("desktop");
        place->label = QObject::tr("Desktop");
        place->hwnd = 0;
        return true;
    }
    if (kTransientClasses.contains(cls) || !IsWindowVisible(hwnd))
        return false;

    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof cloaked);
    if (cloaked)
        return false;

    const QString exe = QFileInfo(path).fileName().toLower();
    if (exe.isEmpty())
        return false;
    const QString title = windowTitle(hwnd);

    place->hwnd = quintptr(hwnd);
    if (kBrowsers.contains(exe)) {
        const QString page = pageFromTitle(title);
        place->key = exe + u'|' + page;
        place->label = page.isEmpty() ? QFileInfo(path).completeBaseName() : page;
    } else if (exe == QLatin1String("applicationframehost.exe")) {
        // Store apps all run under one host; the title is what tells them apart.
        place->key = exe + u'|' + title;
        place->label = title;
    } else {
        place->key = exe;
        place->label = title.isEmpty() ? QFileInfo(path).completeBaseName() : title;
    }
    return true;
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
    const quintptr hwnd = quintptr(GetForegroundWindow());
    Place place;
    if (!placeFor(hwnd, &place))
        return;
    if (place == m_place) {
        m_place.label = place.label;
        return;
    }
    m_place = place;
    emit placeChanged(m_place);
}

void PlaceTracker::onLocation(quintptr hwnd)
{
    if (hwnd && hwnd == m_place.hwnd)
        emit anchorMoved();
}

void PlaceTracker::onName(quintptr hwnd)
{
    // A browser changes its title when the tab changes, which on this layer
    // is the only sign the page did.
    if (hwnd && hwnd == m_place.hwnd && HWND(hwnd) == GetForegroundWindow())
        resolve();
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
