#include "SheetStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

// 1 held one text per place; 2 holds a list of sheets.
constexpr int kFormat = 2;
// In the title bar, left of where the caption buttons usually end.
const QPointF kDefaultHole(170, 18);

bool blank(const QString &text)
{
    return text.trimmed().isEmpty();
}

} // namespace

bool PlaceRecord::hasWriting() const
{
    for (const QString &sheet : sheets) {
        if (!blank(sheet))
            return true;
    }
    return false;
}

SheetStore::SheetStore(QObject *parent)
    : QObject(parent)
    , m_lastHole(kDefaultHole)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(600);
    connect(&m_saveTimer, &QTimer::timeout, this, &SheetStore::flush);
}

SheetStore::~SheetStore()
{
    flush();
}

QString SheetStore::path() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/sheets.json");
}

void SheetStore::load()
{
    QFile file(path());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray last = root.value(QStringLiteral("lastHole")).toArray();
    if (last.size() == 2)
        m_lastHole = QPointF(last.at(0).toDouble(), last.at(1).toDouble());

    const int format = root.value(QStringLiteral("format")).toInt(1);
    const QJsonObject places = root.value(format >= 2 ? QStringLiteral("places") : QStringLiteral("sheets")).toObject();
    for (auto it = places.begin(); it != places.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        PlaceRecord record;
        record.label = o.value(QStringLiteral("label")).toString();
        if (format >= 2) {
            for (const QJsonValue &sheet : o.value(QStringLiteral("sheets")).toArray())
                record.sheets.append(sheet.toString());
        } else {
            record.sheets.append(o.value(QStringLiteral("text")).toString());
        }
        const QJsonArray hole = o.value(QStringLiteral("hole")).toArray();
        if (hole.size() == 2) {
            record.hole = QPointF(hole.at(0).toDouble(), hole.at(1).toDouble());
            record.hasHole = true;
        }
        m_places.insert(it.key(), record);
    }
}

void SheetStore::flush()
{
    m_saveTimer.stop();
    if (!m_dirty)
        return;

    QJsonObject places;
    for (auto it = m_places.cbegin(); it != m_places.cend(); ++it) {
        const PlaceRecord &record = it.value();
        QJsonArray sheets;
        for (const QString &sheet : record.sheets) {
            if (!blank(sheet))
                sheets.append(sheet);
        }
        // Nothing written and the hole where new places put one anyway:
        // nothing worth keeping.
        if (sheets.isEmpty() && !record.hasHole)
            continue;
        QJsonObject o;
        o.insert(QStringLiteral("label"), record.label);
        o.insert(QStringLiteral("sheets"), sheets);
        if (record.hasHole)
            o.insert(QStringLiteral("hole"), QJsonArray{ record.hole.x(), record.hole.y() });
        places.insert(it.key(), o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), kFormat);
    root.insert(QStringLiteral("lastHole"), QJsonArray{ m_lastHole.x(), m_lastHole.y() });
    root.insert(QStringLiteral("places"), places);

    QDir().mkpath(QFileInfo(path()).absolutePath());
    QSaveFile file(path());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        if (file.commit())
            m_dirty = false;
    }
}

PlaceRecord SheetStore::place(const QString &key) const
{
    PlaceRecord record = m_places.value(key);
    if (!record.hasHole)
        record.hole = m_lastHole;
    if (record.sheets.isEmpty())
        record.sheets.append(QString());
    return record;
}

void SheetStore::setSheet(const QString &key, const QString &label, int index, const QString &text)
{
    PlaceRecord &record = m_places[key];
    while (record.sheets.size() <= index)
        record.sheets.append(QString());
    if (record.sheets.at(index) == text && record.label == label)
        return;
    record.sheets[index] = text;
    record.label = label;
    touch();
}

int SheetStore::addSheet(const QString &key, const QString &label)
{
    PlaceRecord &record = m_places[key];
    if (record.sheets.isEmpty())
        record.sheets.append(QString());
    record.sheets.append(QString());
    record.label = label;
    return int(record.sheets.size()) - 1;
}

void SheetStore::removeSheet(const QString &key, int index)
{
    auto it = m_places.find(key);
    if (it == m_places.end() || index < 0 || index >= it->sheets.size())
        return;
    it->sheets.removeAt(index);
    touch();
}

void SheetStore::setHole(const QString &key, const QPointF &hole)
{
    PlaceRecord &record = m_places[key];
    record.hole = hole;
    record.hasHole = true;
    m_lastHole = hole;
    touch();
}

void SheetStore::dropBlanks(const QString &key)
{
    auto it = m_places.find(key);
    if (it == m_places.end())
        return;
    it->sheets.removeIf(blank);
    if (it->sheets.isEmpty() && !it->hasHole)
        m_places.erase(it);
}

void SheetStore::touch()
{
    m_dirty = true;
    m_saveTimer.start();
}
