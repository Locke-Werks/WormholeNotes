#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

class QLocalServer;

// The browser extension's side door. The browser starts
// WormholeNotes.exe as a native messaging host, a second, short-lived copy
// that reads the extension's messages from stdin and passes them on to the
// running WormholeNotes over a local socket. ExtensionLink is the running
// copy's end of that socket.
namespace NativeHost {

inline constexpr char kHostName[] = "com.lockewerks.wormholenotes";
// The extension's ID, fixed by the public key in extension/manifest.json.
inline constexpr char kExtensionId[] = "diognjmabdkecbcibbooldkeppjcacfa";
// Firefox's name for the same extension, from browser_specific_settings.
inline constexpr char kGeckoId[] = "wormholenotes@lockewerks";

// True when a browser started this process as its native host. Chromium
// passes the calling extension's origin as the first argument; Firefox passes
// the host manifest's path, then the add-on's ID.
bool isHostLaunch(int argc, char *argv[]);
// Runs the host until the browser closes the pipe.
int run();
// Registers this executable as the host with every browser that keeps its
// hosts under the user's registry (Chrome, Edge, Brave and Firefox), so
// installing the extension is the only step left. Per user, so no elevation.
void registerForUser();

} // namespace NativeHost

// One report from the extension: the page on screen in the focused window,
// the page on screen in every window, and every open tab, as the browser's
// executable name and pages.
struct ExtensionReport
{
    struct Page
    {
        QString page;  // host grain, as BrowserReader::pageOf gives it
        QString title;
    };
    QString browser; // lower-case exe name
    QList<Page> showing;
    QList<Page> tabs;
};

class ExtensionLink : public QObject
{
    Q_OBJECT

public:
    explicit ExtensionLink(QObject *parent = nullptr);
    bool listen();

Q_SIGNALS:
    void reported(const ExtensionReport &report);

private:
    void onMessage(const QJsonObject &message);

    QLocalServer *m_server = nullptr;
};
