#include "TondoWindow.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Tondo"));
    QApplication::setOrganizationName(QStringLiteral("Locke Werks"));
    QApplication::setApplicationVersion(QStringLiteral(TONDO_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/tondo.ico")));

    TondoWindow window;
    const QStringList args = QApplication::arguments();
    if (args.size() > 1)
        window.openPath(args.at(1));
    window.show();
    return app.exec();
}
