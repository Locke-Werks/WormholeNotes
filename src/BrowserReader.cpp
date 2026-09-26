#include "BrowserReader.h"

#include <QUrl>

#define NOMINMAX
#include <windows.h>
#include <UIAutomation.h>

#include <deque>

namespace {

// A browser's own chrome is a few hundred elements. The cap is for the pages
// that expose their content outside a document anyway.
constexpr int kMaxVisited = 1500;
constexpr int kMaxDepth = 16;

template<typename T>
void release(T *&p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

QString bstrProperty(IUIAutomationElement *element, PROPERTYID property)
{
    VARIANT v;
    VariantInit(&v);
    QString out;
    if (SUCCEEDED(element->GetCurrentPropertyValue(property, &v)) && v.vt == VT_BSTR && v.bstrVal)
        out = QString::fromWCharArray(v.bstrVal, int(SysStringLen(v.bstrVal)));
    VariantClear(&v);
    return out;
}

// Breadth first through the control view, never entering a document, calling
// visit on each element. visit returns false to stop.
template<typename Visit>
void walk(IUIAutomation *automation, IUIAutomationElement *root, Visit visit)
{
    IUIAutomationTreeWalker *walker = nullptr;
    if (FAILED(automation->get_ControlViewWalker(&walker)) || !walker)
        return;

    struct Item
    {
        IUIAutomationElement *element;
        int depth;
    };
    std::deque<Item> queue;
    root->AddRef();
    queue.push_back({ root, 0 });
    int visited = 0;
    bool stop = false;

    while (!queue.empty()) {
        Item item = queue.front();
        queue.pop_front();
        if (stop || ++visited > kMaxVisited) {
            item.element->Release();
            continue;
        }
        CONTROLTYPEID type = 0;
        item.element->get_CurrentControlType(&type);
        if (item.element != root && !visit(item.element, type))
            stop = true;

        if (!stop && type != UIA_DocumentControlTypeId && item.depth < kMaxDepth) {
            IUIAutomationElement *child = nullptr;
            walker->GetFirstChildElement(item.element, &child);
            while (child) {
                queue.push_back({ child, item.depth + 1 });
                IUIAutomationElement *next = nullptr;
                walker->GetNextSiblingElement(child, &next);
                child = next;
            }
        }
        item.element->Release();
    }
    walker->Release();
}

} // namespace

BrowserReader::BrowserReader(QObject *parent)
    : QObject(parent)
{
}

BrowserReader::~BrowserReader()
{
    for (IUIAutomationElement *element : std::as_const(m_bars))
        element->Release();
    m_bars.clear();
    release(m_automation);
    if (m_comReady)
        CoUninitialize();
}

bool BrowserReader::ensure()
{
    // First call on the reader's own thread, which is where COM has to be
    // set up for the calls that follow.
    if (!m_comReady)
        m_comReady = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    if (!m_automation && m_comReady)
        CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_automation));
    return m_automation != nullptr;
}

QString BrowserReader::pageOf(const QString &address)
{
    const QString text = address.trimmed();
    // Whatever is in the bar while someone types a search is not an address.
    if (text.isEmpty() || text.contains(u' '))
        return {};
    const QUrl url = QUrl::fromUserInput(text);
    if (!url.isValid())
        return {};
    const QString scheme = url.scheme().toLower();
    QString host = url.host().toLower();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https")) {
        if (!host.contains(u'.') && host != QLatin1String("localhost"))
            return {};
        // Chrome hides www. and Firefox does not, so neither decides the place.
        if (host.startsWith(QLatin1String("www.")))
            host = host.mid(4);
        // Local servers are told apart by port, not by name.
        if (url.port() > 0)
            host += u':' + QString::number(url.port());
        return host;
    }
    if (scheme == QLatin1String("file"))
        return QStringLiteral("file");
    if (scheme.isEmpty())
        return {};
    return host.isEmpty() ? scheme + u':' + url.path() : scheme + QStringLiteral("://") + host;
}

QString BrowserReader::readValue(IUIAutomationElement *element, bool *focused)
{
    VARIANT v;
    VariantInit(&v);
    *focused = SUCCEEDED(element->GetCurrentPropertyValue(UIA_HasKeyboardFocusPropertyId, &v)) && v.vt == VT_BOOL
        && v.boolVal == VARIANT_TRUE;
    VariantClear(&v);
    return bstrProperty(element, UIA_ValueValuePropertyId);
}

IUIAutomationElement *BrowserReader::addressBar(quintptr hwnd)
{
    if (IUIAutomationElement *cached = m_bars.value(hwnd))
        return cached;

    IUIAutomationElement *root = nullptr;
    if (FAILED(m_automation->ElementFromHandle(HWND(hwnd), &root)) || !root)
        return nullptr;

    // The first edit box in the chrome whose text reads as an address. In
    // every browser that is the address bar, which sits above anything else
    // with a text field in it.
    IUIAutomationElement *found = nullptr;
    walk(m_automation, root, [&](IUIAutomationElement *element, CONTROLTYPEID type) {
        if (type != UIA_EditControlTypeId)
            return true;
        const QString automationId = bstrProperty(element, UIA_AutomationIdPropertyId);
        const QString value = bstrProperty(element, UIA_ValueValuePropertyId);
        if (automationId == QLatin1String("urlbar-input") || !pageOf(value).isEmpty()) {
            element->AddRef();
            found = element;
            return false;
        }
        return true;
    });
    root->Release();
    if (found)
        m_bars.insert(hwnd, found);
    return found;
}

void BrowserReader::forget(quintptr hwnd)
{
    if (IUIAutomationElement *element = m_bars.take(hwnd))
        element->Release();
}

void BrowserReader::readAddress(quintptr hwnd)
{
    QString page;
    if (ensure() && IsWindow(HWND(hwnd))) {
        // A cached bar goes stale when the browser rebuilds its toolbar, so a
        // failed read looks the bar up again once.
        for (int attempt = 0; attempt < 2 && page.isEmpty(); ++attempt) {
            IUIAutomationElement *bar = addressBar(hwnd);
            if (!bar)
                break;
            bool focused = false;
            const QString value = readValue(bar, &focused);
            if (focused)
                break;
            page = pageOf(value);
            if (page.isEmpty())
                forget(hwnd);
        }
    } else {
        forget(hwnd);
    }
    emit addressRead(hwnd, page);
}

void BrowserReader::readTaskbar()
{
    if (!ensure())
        return;
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    // Windows 11 draws the buttons in a XAML island inside the taskbar; the
    // taskbar window itself only holds an empty pane for them.
    HWND island = tray ? FindWindowExW(tray, nullptr, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr) : nullptr;
    IUIAutomationElement *root = nullptr;
    if (!tray || FAILED(m_automation->ElementFromHandle(island ? island : tray, &root)) || !root)
        return;
    // Each window's button names the window itself: "Window: 0x504a0".
    QList<quintptr> windows;
    walk(m_automation, root, [&](IUIAutomationElement *element, CONTROLTYPEID type) {
        if (type != UIA_ButtonControlTypeId)
            return true;
        const QString id = bstrProperty(element, UIA_AutomationIdPropertyId);
        if (id.startsWith(QLatin1String("Window: 0x"))) {
            bool ok = false;
            const quintptr hwnd = id.mid(10).toULongLong(&ok, 16);
            if (ok)
                windows.append(hwnd);
        }
        return true;
    });
    root->Release();
    emit taskbarRead(windows);
}

void BrowserReader::readTabs(QList<quintptr> hwnds)
{
    if (!ensure())
        return;
    for (const quintptr hwnd : hwnds) {
        if (!IsWindow(HWND(hwnd))) {
            forget(hwnd);
            continue;
        }
        IUIAutomationElement *root = nullptr;
        if (FAILED(m_automation->ElementFromHandle(HWND(hwnd), &root)) || !root)
            continue;
        QStringList titles;
        walk(m_automation, root, [&](IUIAutomationElement *element, CONTROLTYPEID type) {
            if (type == UIA_TabItemControlTypeId) {
                const QString name = bstrProperty(element, UIA_NamePropertyId);
                if (!name.isEmpty())
                    titles.append(name);
            }
            return true;
        });
        root->Release();

        QString page;
        if (IUIAutomationElement *bar = addressBar(hwnd)) {
            bool focused = false;
            const QString value = readValue(bar, &focused);
            if (!focused)
                page = pageOf(value);
        }
        emit tabsRead(hwnd, titles, page);
    }
}
