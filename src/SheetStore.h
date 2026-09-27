#pragma once

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QTimer>

#include <functional>

// Everything kept about one place: its sheets and where its hole sits.
struct PlaceRecord
{
    QString label;
    // One thought per sheet, however many pages it runs to. Never empty when
    // handed out: a place with nothing written still has one blank sheet
    // ready, because opening a new page opens a note.
    QStringList sheets;
    // Each sheet's ring colour, as #rrggbb, beside its text; empty for the
    // default. Always the same length as sheets.
    QStringList colours;
    // Where the hole sits on the window, in device-independent pixels: x from
    // the window's right edge, y from its top. Right-anchored, because that is
    // the corner a hole punch goes through and it survives a window resize.
    QPointF hole;
    bool hasHole = false;
    // A note torn off onto the desktop: where it sits, in logical screen
    // coordinates.
    QPoint desk;
    bool onDesk = false;

    bool hasWriting() const;
};

// Every place's sheets, in one JSON file under the user's local app data.
// Local only, never synced. Writes are batched so typing does not rewrite the
// file on every keystroke, and each write replaces the file atomically.
// Blank sheets are never written, so they close with their page.
class SheetStore : public QObject
{
    Q_OBJECT

public:
    explicit SheetStore(QObject *parent = nullptr);
    ~SheetStore() override;

    void load();
    void flush();
    QString path() const;

    PlaceRecord place(const QString &key) const;
    void setSheet(const QString &key, const QString &label, int index, const QString &text);
    void setColour(const QString &key, int index, const QString &colour);
    // Adds a blank sheet at the end and returns its index.
    int addSheet(const QString &key, const QString &label);
    void removeSheet(const QString &key, int index);
    void setHole(const QString &key, const QPointF &hole);
    // The page closed: its blank sheets go with it.
    void dropBlanks(const QString &key);

    // Desk notes live under keys of their own, "desk:" and an id.
    QString newDeskNote(const QStringList &sheets, const QPoint &at, const QStringList &colours = {});
    void setDesk(const QString &key, const QPoint &at);
    QStringList deskKeys() const;
    // Every place with a record, desk notes included.
    QStringList keys() const;

    // A dated copy of the sheets file in a backups folder beside it, once a
    // day, keeping the newest `keep` days. Returns the copy's path when one
    // was made today by this call.
    QString backupDaily(const QDate &today = QDate::currentDate(), int keep = 14);
    QString backupDirectory() const;
    struct Backup
    {
        QString path;
        QDateTime taken; // a daily copy's day at midnight
        // The notes a restore replaced, kept so the restore can be undone.
        bool beforeRestore = false;
    };
    // Every backup, newest first.
    QList<Backup> backups() const;
    // Makes a backup the current notes, keeping the notes it replaces as a
    // before-restore backup first. False, with nothing changed, when the
    // backup cannot be read.
    bool restore(const QString &backupPath);
    // Every written sheet as one Markdown document, a section per place,
    // named by `label`.
    QString exportMarkdown(const std::function<QString(const QString &key)> &label) const;
    void forget(const QString &key);

    // Where a place that has never had a hole gets one: where the last hole
    // was put.
    QPointF lastHole() const { return m_lastHole; }

private:
    void touch();
    bool read(const QString &from, QHash<QString, PlaceRecord> *places, QPointF *lastHole) const;

    QHash<QString, PlaceRecord> m_places;
    QPointF m_lastHole;
    QTimer m_saveTimer;
    bool m_dirty = false;
};
