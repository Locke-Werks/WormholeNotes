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
        store.setColour(QStringLiteral("claude.exe"), 1, QStringLiteral("#ff2d95"));
        store.flush();
        SheetStore reread;
        reread.load();
        check(reread.place(QStringLiteral("claude.exe")).colours
                  == (QStringList{ QString(), QStringLiteral("#ff2d95") }),
              "a sheet keeps its ring colour, and one never set stays the default");
        store.removeSheet(QStringLiteral("claude.exe"), 0);
        check(store.place(QStringLiteral("claude.exe")).colours == QStringList{ QStringLiteral("#ff2d95") },
              "the colour goes with its sheet when another is deleted");
        check(store.place(QStringLiteral("claude.exe")).sheets == QStringList{ QStringLiteral("new thought") },
              "a sheet can be deleted");
    }

    {
        SheetStore store;
        store.load();
        const QString desk = store.newDeskNote({ QStringLiteral("torn off") }, QPoint(300, 400));
        check(store.deskKeys() == QStringList{ desk }, "a torn-off note is a desk note");
        store.flush();
        SheetStore again;
        again.load();
        check(again.place(desk).onDesk && again.place(desk).desk == QPoint(300, 400), "a desk note keeps its spot");
        again.forget(desk);
        check(again.deskKeys().isEmpty(), "a desk note dragged back onto a page is gone from the desk");
        const QString blank = again.newDeskNote({ QString() }, QPoint(1, 1));
        again.flush();
        SheetStore third;
        third.load();
        check(!third.deskKeys().contains(blank), "a blank desk note is not kept");
    }

    {
        SheetStore store;
        store.load();
        store.setSheet(QStringLiteral("notepad.exe"), QStringLiteral("Notepad"), 0, QStringLiteral("first thought"));
        const int second = store.addSheet(QStringLiteral("notepad.exe"), QStringLiteral("Notepad"));
        store.setSheet(QStringLiteral("notepad.exe"), QStringLiteral("Notepad"), second, QStringLiteral("second thought"));
        const QString md = store.exportMarkdown([](const QString &key) { return key == QLatin1String("notepad.exe") ? QStringLiteral("Notepad") : key; });
        check(md.contains(QStringLiteral("## Notepad")) && md.contains(QStringLiteral("first thought"))
                  && md.contains(QStringLiteral("second thought")),
              "the export has a section per place with its sheets");
        store.setSheet(QStringLiteral("notepad.exe"), QStringLiteral("Notepad"), 0, QStringLiteral("line one \nline two"));
        check(store.exportMarkdown([](const QString &key) { return key; }).contains(QStringLiteral("line one  \nline two\n")),
              "a sheet's line breaks survive as Markdown hard breaks");

        QDir(store.backupDirectory()).removeRecursively();
        const QDate day(2026, 9, 1);
        check(!store.backupDaily(day, 3).isEmpty(), "a day's first backup is made");
        check(store.backupDaily(day, 3).isEmpty(), "a second backup the same day is not");
        for (int i = 1; i <= 5; ++i)
            store.backupDaily(day.addDays(i), 3);
        const QStringList kept = QDir(store.backupDirectory()).entryList({ QStringLiteral("sheets-*.json") }, QDir::Files, QDir::Name);
        check(kept == (QStringList{ QStringLiteral("sheets-2026-09-04.json"), QStringLiteral("sheets-2026-09-05.json"),
                                    QStringLiteral("sheets-2026-09-06.json") }),
              "only the newest days are kept");
        QDir(store.backupDirectory()).removeRecursively();

        // Restore: yesterday's copy comes back, and today's notes are kept
        // so the restore itself can be undone.
        store.setSheet(QStringLiteral("notepad.exe"), QStringLiteral("Notepad"), 0, QStringLiteral("yesterday"));
        store.backupDaily(QDate(2026, 9, 26));
        store.setSheet(QStringLiteral("notepad.exe"), QStringLiteral("Notepad"), 0, QStringLiteral("today"));
        const QList<SheetStore::Backup> before = store.backups();
        check(before.size() == 1 && !before.constFirst().beforeRestore, "the daily copy is listed");
        check(store.restore(before.constFirst().path)
                  && store.place(QStringLiteral("notepad.exe")).sheets.constFirst() == QLatin1String("yesterday"),
              "restoring a day brings its notes back");
        const QList<SheetStore::Backup> after = store.backups();
        check(after.size() == 2 && after.constFirst().beforeRestore, "the replaced notes are kept, listed first");
        check(store.restore(after.constFirst().path)
                  && store.place(QStringLiteral("notepad.exe")).sheets.constFirst() == QLatin1String("today"),
              "the replaced notes can be restored in turn");
        {
            SheetStore reread;
            reread.load();
            check(reread.place(QStringLiteral("notepad.exe")).sheets.constFirst() == QLatin1String("today"),
                  "a restore is written to disk");
        }
        QFile broken(QDir(store.backupDirectory()).filePath(QStringLiteral("sheets-2026-09-20.json")));
        broken.open(QIODevice::WriteOnly);
        broken.write("{ not json");
        broken.close();
        const qsizetype listed = store.backups().size();
        check(!store.restore(broken.fileName())
                  && store.place(QStringLiteral("notepad.exe")).sheets.constFirst() == QLatin1String("today")
                  && store.backups().size() == listed,
              "a broken backup changes nothing");
        QDir(store.backupDirectory()).removeRecursively();
    }

    QFile::remove(SheetStore().path());
    return failures == 0 ? 0 : 1;
}
