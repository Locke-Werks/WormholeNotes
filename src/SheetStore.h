#pragma once

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QTimer>

// A place's note.
struct Sheet
{
    QString label;
    QString text;
    // Where the hole sits on the window, in device-independent pixels: x from
    // the window's right edge, y from its top. Right-anchored, because that is
    // the corner a hole punch goes through and it survives a window resize.
    QPointF hole;
    bool hasHole = false;
};

// Every sheet, in one JSON file under the user's local app data. Local only,
// never synced. Writes are batched so typing does not rewrite the file on
// every keystroke, and each write replaces the file atomically.
class SheetStore : public QObject
{
    Q_OBJECT

public:
    explicit SheetStore(QObject *parent = nullptr);
    ~SheetStore() override;

    void load();
    void flush();
    QString path() const;

    Sheet sheet(const QString &key) const;
    void setText(const QString &key, const QString &label, const QString &text);
    void setHole(const QString &key, const QPointF &hole);

    // Where a place that has never had a hole gets one: where the last hole
    // was put.
    QPointF lastHole() const { return m_lastHole; }

private:
    void touch();

    QHash<QString, Sheet> m_sheets;
    QPointF m_lastHole;
    QTimer m_saveTimer;
    bool m_dirty = false;
};
