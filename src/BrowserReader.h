#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>

struct IUIAutomation;
struct IUIAutomationElement;

// Reads a browser window through UI Automation: the address bar, for the page
// the user is on, and the tab strip, for which pages are still open. It lives
// on its own thread, because a UI Automation call waits on the browser and a
// busy browser can take a long time to answer.
//
// It also reads the order of the taskbar's buttons, which is what the ring of
// places follows.
//
// Only the browser's own chrome is searched. Web content can hold tens of
// thousands of elements, so any subtree that is a document is skipped.
class BrowserReader : public QObject
{
    Q_OBJECT

public:
    explicit BrowserReader(QObject *parent = nullptr);
    ~BrowserReader() override;

    // Turns an address bar's text into a place's page part: the host for web
    // pages, which is the subdomain grain places are kept at, else the scheme
    // and host. Empty when the text is not an address.
    static QString pageOf(const QString &address);

public Q_SLOTS:
    void readAddress(quintptr hwnd);
    void readTabs(QList<quintptr> hwnds);
    void readTaskbar();

Q_SIGNALS:
    // Empty page: could not be read, or the user is typing in the bar.
    void addressRead(quintptr hwnd, const QString &page);
    // The window's tabs, and the page its active tab is on.
    void tabsRead(quintptr hwnd, const QStringList &titles, const QString &page);
    // The windows on the taskbar, in button order left to right.
    void taskbarRead(const QList<quintptr> &windows);

private:
    bool ensure();
    IUIAutomationElement *addressBar(quintptr hwnd);
    QString readValue(IUIAutomationElement *element, bool *focused);
    void forget(quintptr hwnd);

    IUIAutomation *m_automation = nullptr;
    bool m_comReady = false;
    QHash<quintptr, IUIAutomationElement *> m_bars;
};
