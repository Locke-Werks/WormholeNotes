#include "NativeHost.h"
#include "SignIn.h"
#include "Theme.h"
#include "Wormhole.h"

#include <QApplication>
#include <QIcon>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int main(int argc, char *argv[])
{
    // Started by a browser for its extension: relay, and nothing else. This
    // copy never takes the single-instance lock or shows a window.
    if (NativeHost::isHostLaunch(argc, argv)) {
        QCoreApplication host(argc, argv);
        QCoreApplication::setOrganizationName(QStringLiteral("Locke Werks"));
        QCoreApplication::setApplicationName(QStringLiteral("WormholeNotes"));
        return NativeHost::run();
    }

    // Run by the uninstaller, as the user, to take back what this copy wrote
    // into their profile: the browser registration and their own sign-in
    // entry.
    if (argc > 1 && qstrcmp(argv[1], "--unregister-browsers") == 0) {
        QCoreApplication tool(argc, argv);
        QCoreApplication::setOrganizationName(QStringLiteral("Locke Werks"));
        QCoreApplication::setApplicationName(QStringLiteral("WormholeNotes"));
        NativeHost::unregisterForUser();
        SignIn::forget();
        return 0;
    }

    // Started at sign-in by an entry this user has turned off in the settings.
    QCoreApplication::setOrganizationName(QStringLiteral("Locke Werks"));
    QCoreApplication::setApplicationName(QStringLiteral("WormholeNotes"));
    if (SignIn::shouldQuit(argc, argv))
        return 0;

    // One hole per desktop. A second copy would put a second hole on every
    // window and write the same sheets file from two processes.
    HANDLE instance = CreateMutexW(nullptr, TRUE, L"LockeWerks.WormholeNotes.Instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
        return 0;

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("WormholeNotes"));
    QApplication::setOrganizationName(QStringLiteral("Locke Werks"));
    QApplication::setApplicationVersion(QStringLiteral(WORMHOLENOTES_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/wormholenotes.ico")));
    // The note hides rather than closes, and the hole is never the last
    // window in any sense that matters. Quitting is explicit.
    QApplication::setQuitOnLastWindowClosed(false);
    Theme::loadFonts();
    app.setStyleSheet(Theme::styleSheet());

    Wormhole wormhole;
    wormhole.start();
    const int code = app.exec();

    CloseHandle(instance);
    return code;
}
