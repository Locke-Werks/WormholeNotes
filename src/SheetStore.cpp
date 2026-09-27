#include "SheetStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDateTime>
#include <QUuid>

#include <algorithm>

namespace {

// 1 held one text per place; 2 a list of sheets; 3 a list of sheets, each
// with its ring colour.
constexpr int kFormat = 3;
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
    if (last.size() == 2 && last.at(0).toDouble() >= 0 && last.at(1).toDouble() >= 0)
        m_lastHole = QPointF(last.at(0).toDouble(), last.at(1).toDouble());

    const int format = root.value(QStringLiteral("format")).toInt(1);
    const QJsonObject places = root.value(format >= 2 ? QStringLiteral("places") : QStringLiteral("sheets")).toObject();
    for (auto it = places.begin(); it != places.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        PlaceRecord record;
        record.label = o.value(QStringLiteral("label")).toString();
        if (format >= 3) {
            for (const QJsonValue &sheet : o.value(QStringLiteral("sheets")).toArray()) {
                record.sheets.append(sheet.toObject().value(QStringLiteral("text")).toString());
                record.colours.append(sheet.toObject().value(QStringLiteral("colour")).toString());
            }
        } else if (format == 2) {
            for (const QJsonValue &sheet : o.value(QStringLiteral("sheets")).toArray())
                record.sheets.append(sheet.toString());
        } else {
            record.sheets.append(o.value(QStringLiteral("text")).toString());
        }
        while (record.colours.size() < record.sheets.size())
            record.colours.append(QString());
        const QJsonArray hole = o.value(QStringLiteral("hole")).toArray();
        if (hole.size() == 2 && hole.at(0).toDouble() >= 0 && hole.at(1).toDouble() >= 0) {
            record.hole = QPointF(hole.at(0).toDouble(), hole.at(1).toDouble());
            record.hasHole = true;
        }
        const QJsonArray desk = o.value(QStringLiteral("desk")).toArray();
        if (desk.size() == 2) {
            record.desk = QPoint(desk.at(0).toInt(), desk.at(1).toInt());
            record.onDesk = true;
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
        for (int i = 0; i < record.sheets.size(); ++i) {
            if (blank(record.sheets.at(i)))
                continue;
            QJsonObject sheet;
            sheet.insert(QStringLiteral("text"), record.sheets.at(i));
            if (!record.colours.value(i).isEmpty())
                sheet.insert(QStringLiteral("colour"), record.colours.at(i));
            sheets.append(sheet);
        }
        // Nothing written and the hole where new places put one anyway:
        // nothing worth keeping. A desk note is nothing but its writing.
        if (sheets.isEmpty() && (!record.hasHole || record.onDesk))
            continue;
        QJsonObject o;
        o.insert(QStringLiteral("label"), record.label);
        o.insert(QStringLiteral("sheets"), sheets);
        if (record.hasHole)
            o.insert(QStringLiteral("hole"), QJsonArray{ record.hole.x(), record.hole.y() });
        if (record.onDesk)
            o.insert(QStringLiteral("desk"), QJsonArray{ record.desk.x(), record.desk.y() });
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
    while (record.colours.size() < record.sheets.size())
        record.colours.append(QString());
    return record;
}

void SheetStore::setSheet(const QString &key, const QString &label, int index, const QString &text)
{
    PlaceRecord &record = m_places[key];
    while (record.sheets.size() <= index)
        record.sheets.append(QString());
    while (record.colours.size() < record.sheets.size())
        record.colours.append(QString());
    if (record.sheets.at(index) == text && record.label == label)
        return;
    record.sheets[index] = text;
    record.label = label;
    touch();
}

void SheetStore::setColour(const QString &key, int index, const QString &colour)
{
    PlaceRecord &record = m_places[key];
    while (record.sheets.size() <= index)
        record.sheets.append(QString());
    while (record.colours.size() < record.sheets.size())
        record.colours.append(QString());
    if (record.colours.at(index) == colour)
        return;
    record.colours[index] = colour;
    touch();
}

int SheetStore::addSheet(const QString &key, const QString &label)
{
    PlaceRecord &record = m_places[key];
    if (record.sheets.isEmpty())
        record.sheets.append(QString());
    while (record.colours.size() < record.sheets.size())
        record.colours.append(QString());
    record.sheets.append(QString());
    record.colours.append(QString());
    record.label = label;
    return int(record.sheets.size()) - 1;
}

void SheetStore::removeSheet(const QString &key, int index)
{
    auto it = m_places.find(key);
    if (it == m_places.end() || index < 0 || index >= it->sheets.size())
        return;
    it->sheets.removeAt(index);
    if (index < it->colours.size())
        it->colours.removeAt(index);
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
    for (int i = int(it->sheets.size()) - 1; i >= 0; --i) {
        if (blank(it->sheets.at(i))) {
            it->sheets.removeAt(i);
            if (i < it->colours.size())
                it->colours.removeAt(i);
        }
    }
    if (it->sheets.isEmpty() && !it->hasHole)
        m_places.erase(it);
}

QString SheetStore::newDeskNote(const QStringList &sheets, const QPoint &at, const QStringList &colours)
{
    const QString key = QStringLiteral("desk:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    PlaceRecord &record = m_places[key];
    record.sheets = sheets;
    record.colours = colours;
    while (record.colours.size() < record.sheets.size())
        record.colours.append(QString());
    record.desk = at;
    record.onDesk = true;
    touch();
    return key;
}

void SheetStore::setDesk(const QString &key, const QPoint &at)
{
    PlaceRecord &record = m_places[key];
    record.desk = at;
    record.onDesk = true;
    touch();
}

QStringList SheetStore::deskKeys() const
{
    QStringList keys;
    for (auto it = m_places.cbegin(); it != m_places.cend(); ++it) {
        if (it->onDesk && it->hasWriting())
            keys.append(it.key());
    }
    keys.sort();
    return keys;
}

QString SheetStore::backupDirectory() const
{
    return QFileInfo(path()).absolutePath() + QStringLiteral("/backups");
}

QString SheetStore::backupDaily(const QDate &today, int keep)
{
    flush();
    if (!QFile::exists(path()))
        return {};
    QDir dir(backupDirectory());
    dir.mkpath(QStringLiteral("."));
    const QString name = QStringLiteral("sheets-%1.json").arg(today.toString(Qt::ISODate));
    QString made;
    if (!dir.exists(name) && QFile::copy(path(), dir.filePath(name)))
        made = dir.filePath(name);
    // The dated names sort by date, so the oldest are at the front.
    QStringList copies = dir.entryList({ QStringLiteral("sheets-????-??-??.json") }, QDir::Files, QDir::Name);
    while (copies.size() > keep)
        dir.remove(copies.takeFirst());
    return made;
}

QString SheetStore::exportMarkdown(const std::function<QString(const QString &key)> &label) const
{
    struct Entry
    {
        QString key;
        QString name;
    };
    QList<Entry> places;
    QList<Entry> desk;
    for (auto it = m_places.cbegin(); it != m_places.cend(); ++it) {
        if (!it->hasWriting())
            continue;
        (it->onDesk ? desk : places).append({ it.key(), label(it.key()) });
    }
    const auto byName = [](const Entry &a, const Entry &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; };
    std::sort(places.begin(), places.end(), byName);
    std::sort(desk.begin(), desk.end(), byName);

    QString out = QStringLiteral("# WormholeNotes\n\nExported %1.\n")
                      .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    const auto section = [&](const Entry &entry) {
        out += QStringLiteral("\n## %1\n").arg(entry.name);
        if (entry.name != entry.key && !entry.key.startsWith(QLatin1String("desk:")))
            out += QStringLiteral("\n`%1`\n").arg(entry.key);
        const PlaceRecord &record = m_places[entry.key];
        int shown = 0;
        for (const QString &sheet : record.sheets) {
            if (blank(sheet))
                continue;
            out += shown++ ? QStringLiteral("\n---\n\n") : QStringLiteral("\n");
            out += sheet.trimmed() + u'\n';
        }
    };
    for (const Entry &entry : std::as_const(places))
        section(entry);
    if (!desk.isEmpty()) {
        out += QStringLiteral("\n# On the desktop\n");
        for (const Entry &entry : std::as_const(desk))
            section(entry);
    }
    return out;
}

QStringList SheetStore::keys() const
{
    return m_places.keys();
}

void SheetStore::forget(const QString &key)
{
    if (m_places.remove(key))
        touch();
}

void SheetStore::touch()
{
    m_dirty = true;
    m_saveTimer.start();
}
