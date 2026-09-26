#include "Wormhole.h"

#include <QApplication>
#include <QIcon>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int main(int argc, char *argv[])
{
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

    Wormhole wormhole;
    wormhole.start();
    const int code = app.exec();

    CloseHandle(instance);
    return code;
}
