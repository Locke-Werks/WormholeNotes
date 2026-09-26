// Prints what the tracker sees without touching focus or showing a window:
// the current place, the open places in ring order, and an address-bar read
// and tab list for every browser window. For checking detection on a real
// desktop.

#include "../src/BrowserReader.h"
#include "../src/PlaceTracker.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#define NOMINMAX
#include <windows.h>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    PlaceTracker tracker;
    tracker.start();

    BrowserReader reader;
    QList<quintptr> browsers;
    EnumWindows(
        [](HWND hwnd, LPARAM data) -> BOOL {
            wchar_t cls[64] = {};
            GetClassNameW(hwnd, cls, 64);
            if (IsWindowVisible(hwnd) && (wcscmp(cls, L"Chrome_WidgetWin_1") == 0 || wcscmp(cls, L"MozillaWindowClass") == 0)
                && GetWindowTextLengthW(hwnd) > 0)
                reinterpret_cast<QList<quintptr> *>(data)->append(quintptr(hwnd));
            return TRUE;
        },
        LPARAM(&browsers));

    QObject::connect(&reader, &BrowserReader::addressRead, [&](quintptr hwnd, const QString &page) {
        out << "address " << hwnd << ": " << (page.isEmpty() ? QStringLiteral("(none)") : page) << Qt::endl;
    });
    QObject::connect(&reader, &BrowserReader::tabsRead, [&](quintptr hwnd, const QStringList &tabs, const QString &page) {
        out << "tabs " << hwnd << ": " << tabs.size() << " on " << page << Qt::endl;
        for (const QString &tab : tabs)
            out << "    " << tab << Qt::endl;
    });

    QTimer::singleShot(2500, [&] {
        out << "current: " << tracker.current().key << "  (" << tracker.current().label << ")" << Qt::endl;
        out << "open:" << Qt::endl;
        for (const Place &place : tracker.openPlaces())
            out << "    " << place.key << "  (" << place.label << ")" << Qt::endl;
        for (const quintptr hwnd : browsers) {
            QElapsedTimer timer;
            timer.start();
            reader.readAddress(hwnd);
            out << "    read took " << timer.elapsed() << " ms" << Qt::endl;
        }
        if (!browsers.isEmpty()) {
            QElapsedTimer timer;
            timer.start();
            reader.readTabs(browsers);
            out << "    tab walk took " << timer.elapsed() << " ms" << Qt::endl;
        }
        for (const QString &sample : { QStringLiteral("github.com/Locke-Werks"), QStringLiteral("https://www.google.com/x"),
                                       QStringLiteral("mail.google.com"), QStringLiteral("chrome://settings/"),
                                       QStringLiteral("how do wormholes work"), QStringLiteral("localhost:5173/app") })
            out << "pageOf(" << sample << ") = " << BrowserReader::pageOf(sample) << Qt::endl;
        QTimer::singleShot(4000, [&] {
            out << "open after a tab walk:" << Qt::endl;
            for (const Place &place : tracker.openPlaces())
                out << "    " << place.key << "  (" << place.label << ")" << Qt::endl;
            app.quit();
        });
    });
    return app.exec();
}
