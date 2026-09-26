#include "TondoWindow.h"

#include <QApplication>
#include <QIcon>
#include <QSessionManager>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Tondo"));
    QApplication::setOrganizationName(QStringLiteral("Locke Werks"));
    QApplication::setApplicationVersion(QStringLiteral(TONDO_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/tondo.ico")));

    TondoWindow window;

    // Shown before the file opens, so that if it cannot be read the answer
    // appears on a window that exists.
    window.show();
    const QStringList args = QApplication::arguments();
    if (args.size() > 1)
        window.openPath(args.at(1));

    // Windows ends the session by ending the process. With unsaved work, ask it
    // not to, the way Notepad does; the user then sees Tondo holding up sign-out.
    QObject::connect(&app, &QGuiApplication::commitDataRequest, &window, [&window](QSessionManager &manager) {
        if (window.hasUnsavedChanges())
            manager.cancel();
    });

    return app.exec();
}
