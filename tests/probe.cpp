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

    // --extension: stand in for WormholeNotes on the host's socket and print
    // every report the extension's host relays, for ten seconds.
    if (argc > 1 && qstrcmp(argv[1], "--extension") == 0) {
        ExtensionLink link;
        out << (link.listen() ? "listening" : "could not listen") << Qt::endl;
        QObject::connect(&link, &ExtensionLink::reported, [&](const ExtensionReport &report) {
            out << "report from " << report.browser << Qt::endl;
            for (const auto &page : report.showing)
                out << "    showing " << page.page << "  (" << page.title << ")" << Qt::endl;
            for (const auto &page : report.tabs)
                out << "    tab " << page.page << "  (" << page.title << ")" << Qt::endl;
        });
        QTimer::singleShot(10000, &app, &QCoreApplication::quit);
        return app.exec();
    }

    PlaceTracker tracker;
    tracker.start();

    // --watch N: for N seconds, every change of the window in front and
    // every change of place, to see which windows the hole would follow.
    if (argc > 2 && qstrcmp(argv[1], "--watch") == 0) {
        const auto describe = [](quintptr hwnd) {
            wchar_t cls[128] = {}, title[128] = {};
            GetClassNameW(HWND(hwnd), cls, 128);
            GetWindowTextW(HWND(hwnd), title, 128);
            return QStringLiteral("%1 %2 \"%3\"").arg(hwnd).arg(QString::fromWCharArray(cls), QString::fromWCharArray(title));
        };
        QObject::connect(&tracker, &PlaceTracker::placeChanged, [&](const Place &place) {
            out << "place  " << place.key << " on " << describe(place.hwnd) << Qt::endl;
        });
        auto *poll = new QTimer(&app);
        quintptr last = 0;
        QObject::connect(poll, &QTimer::timeout, [&] {
            const quintptr now = quintptr(GetForegroundWindow());
            if (now != last)
                out << "front  " << describe(now) << Qt::endl;
            last = now;
        });
        poll->start(100);
        QTimer::singleShot(QString::fromLatin1(argv[2]).toInt() * 1000, &app, &QCoreApplication::quit);
        return app.exec();
    }

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

    QObject::connect(&reader, &BrowserReader::taskbarRead, [&](const QList<quintptr> &windows) {
        out << "taskbar:";
        for (const quintptr hwnd : windows)
            out << " " << hwnd;
        out << Qt::endl;
    });

    QTimer::singleShot(2500, [&] {
        reader.readTaskbar();
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
