// SheetStore: reading the one-text-per-place format, several sheets per place,
// and blank sheets closing with their page. Runs against Qt's test-mode
// standard paths, never the real sheets file.

#include "../src/SheetStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

static int failures = 0;

static void check(bool ok, const char *what)
{
    QTextStream(stdout) << (ok ? "ok   " : "FAIL ") << what << Qt::endl;
    if (!ok)
        ++failures;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Locke Werks"));
    QCoreApplication::setApplicationName(QStringLiteral("WormholeNotesStoreTest"));
    QStandardPaths::setTestModeEnabled(true);

    {
        SheetStore store;
        QDir().mkpath(QFileInfo(store.path()).absolutePath());
        QFile file(store.path());
        file.open(QIODevice::WriteOnly);
        file.write(R"({"format":1,"lastHole":[120,20],"sheets":{"claude.exe":{"label":"Claude","text":"old note"}}})");
    }

    {
        SheetStore store;
        store.load();
        const PlaceRecord claude = store.place(QStringLiteral("claude.exe"));
        check(claude.sheets == QStringList{ QStringLiteral("old note") }, "format 1 text becomes the first sheet");
        check(claude.hole == QPointF(120, 20), "a place without its own hole gets the last one");

        const PlaceRecord fresh = store.place(QStringLiteral("notepad.exe"));
        check(fresh.sheets == QStringList{ QString() }, "a new place has one blank sheet ready");
        check(!fresh.hasWriting(), "a blank sheet is not writing");

        const int second = store.addSheet(QStringLiteral("claude.exe"), QStringLiteral("Claude"));
        store.setSheet(QStringLiteral("claude.exe"), QStringLiteral("Claude"), second, QStringLiteral("new thought"));
        store.addSheet(QStringLiteral("claude.exe"), QStringLiteral("Claude"));
        check(store.place(QStringLiteral("claude.exe")).sheets.size() == 3, "sheets add at the end");

        store.setSheet(QStringLiteral("chrome.exe|github.com"), QStringLiteral("github.com"), 0, QStringLiteral("   "));
        store.dropBlanks(QStringLiteral("chrome.exe|github.com"));
        store.dropBlanks(QStringLiteral("claude.exe"));
        check(store.place(QStringLiteral("claude.exe")).sheets.size() == 2, "closing the page drops its blank sheet");
        check(!store.place(QStringLiteral("chrome.exe|github.com")).hasWriting(), "a page with only blanks is gone");
        store.flush();
    }

    {
        SheetStore store;
        store.load();
        const QStringList sheets = store.place(QStringLiteral("claude.exe")).sheets;
        check(sheets == (QStringList{ QStringLiteral("old note"), QStringLiteral("new thought") }),
              "format 2 round-trips the sheets");
        store.removeSheet(QStringLiteral("claude.exe"), 0);
        check(store.place(QStringLiteral("claude.exe")).sheets == QStringList{ QStringLiteral("new thought") },
              "a sheet can be deleted");
    }

    QFile::remove(SheetStore().path());
    return failures == 0 ? 0 : 1;
}
