// Renders the note to PNG files offscreen, with a sample ring of places and
// sheets, so the bezel can be looked at without putting a window on screen.
// Usage: wormhole-render <output directory>

#include "../src/NoteWindow.h"

#include <QApplication>
#include <QDir>
#include <QGuiApplication>
#include <QStyleHints>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // The note keeps its settings in QSettings; keep the render's out of the
    // real app's.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::tempPath());
    const QString dir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath();

    NoteWindow note;
    const QList<RingPlace> ring = {
        { QStringLiteral("desktop"), QStringLiteral("Desktop"), false },
        { QStringLiteral("explorer.exe"), QStringLiteral("File Explorer"), false },
        { QStringLiteral("chrome.exe|calendar.google.com"), QStringLiteral("calendar.google.com"), true },
        { QStringLiteral("chrome.exe|github.com"), QStringLiteral("github.com"), true },
        { QStringLiteral("claude.exe"), QStringLiteral("Claude"), false },
        { QStringLiteral("notepad.exe"), QStringLiteral("Notepad"), true },
    };
    note.setRing(ring, 3);
    note.setSheet(QStringLiteral("github.com"),
                  QStringLiteral("Review the Forge schema before wiring the installer.\nAsk Archon about the ring layout."),
                  1, 3);
    note.resize(472, 472);
    note.grab().save(dir + QStringLiteral("/note-ring.png"));

    note.setRing(ring, 0);
    note.setSheet(QStringLiteral("Desktop"), QString(), 0, 1);
    note.grab().save(dir + QStringLiteral("/note-desktop.png"));

    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    QCoreApplication::processEvents();
    note.setRing(ring, 3);
    note.setSheet(QStringLiteral("github.com"),
                  QStringLiteral("Review the Forge schema before wiring the installer.\nAsk Archon about the ring layout."),
                  1, 3);
    note.grab().save(dir + QStringLiteral("/note-light.png"));
    return 0;
}
