#pragma once

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QTimer>

// Everything kept about one place: its sheets and where its hole sits.
struct PlaceRecord
{
    QString label;
    // One thought per sheet, however many pages it runs to. Never empty when
    // handed out: a place with nothing written still has one blank sheet
    // ready, because opening a new page opens a note.
    QStringList sheets;
    // Where the hole sits on the window, in device-independent pixels: x from
    // the window's right edge, y from its top. Right-anchored, because that is
    // the corner a hole punch goes through and it survives a window resize.
    QPointF hole;
    bool hasHole = false;

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
    // Adds a blank sheet at the end and returns its index.
    int addSheet(const QString &key, const QString &label);
    void removeSheet(const QString &key, int index);
    void setHole(const QString &key, const QPointF &hole);
    // The page closed: its blank sheets go with it.
    void dropBlanks(const QString &key);

    // Where a place that has never had a hole gets one: where the last hole
    // was put.
    QPointF lastHole() const { return m_lastHole; }

private:
    void touch();

    QHash<QString, PlaceRecord> m_places;
    QPointF m_lastHole;
    QTimer m_saveTimer;
    bool m_dirty = false;
};
