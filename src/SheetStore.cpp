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

constexpr int kFormat = 1;
// In the title bar, left of where the caption buttons usually end.
const QPointF kDefaultHole(170, 18);

} // namespace

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

    const QJsonObject sheets = root.value(QStringLiteral("sheets")).toObject();
    for (auto it = sheets.begin(); it != sheets.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Sheet sheet;
        sheet.label = o.value(QStringLiteral("label")).toString();
        sheet.text = o.value(QStringLiteral("text")).toString();
        const QJsonArray hole = o.value(QStringLiteral("hole")).toArray();
        if (hole.size() == 2) {
            sheet.hole = QPointF(hole.at(0).toDouble(), hole.at(1).toDouble());
            sheet.hasHole = true;
        }
        m_sheets.insert(it.key(), sheet);
    }
}

void SheetStore::flush()
{
    m_saveTimer.stop();
    if (!m_dirty)
        return;

    QJsonObject sheets;
    for (auto it = m_sheets.cbegin(); it != m_sheets.cend(); ++it) {
        const Sheet &sheet = it.value();
        // A blank sheet with its hole where new places put one anyway carries
        // nothing worth keeping.
        if (sheet.text.isEmpty() && !sheet.hasHole)
            continue;
        QJsonObject o;
        o.insert(QStringLiteral("label"), sheet.label);
        o.insert(QStringLiteral("text"), sheet.text);
        if (sheet.hasHole)
            o.insert(QStringLiteral("hole"), QJsonArray{ sheet.hole.x(), sheet.hole.y() });
        sheets.insert(it.key(), o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), kFormat);
    root.insert(QStringLiteral("lastHole"), QJsonArray{ m_lastHole.x(), m_lastHole.y() });
    root.insert(QStringLiteral("sheets"), sheets);

    QDir().mkpath(QFileInfo(path()).absolutePath());
    QSaveFile file(path());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        if (file.commit())
            m_dirty = false;
    }
}

Sheet SheetStore::sheet(const QString &key) const
{
    Sheet sheet = m_sheets.value(key);
    if (!sheet.hasHole)
        sheet.hole = m_lastHole;
    return sheet;
}

void SheetStore::setText(const QString &key, const QString &label, const QString &text)
{
    Sheet &sheet = m_sheets[key];
    if (sheet.text == text && sheet.label == label)
        return;
    sheet.text = text;
    sheet.label = label;
    touch();
}

void SheetStore::setHole(const QString &key, const QPointF &hole)
{
    Sheet &sheet = m_sheets[key];
    sheet.hole = hole;
    sheet.hasHole = true;
    m_lastHole = hole;
    touch();
}

void SheetStore::touch()
{
    m_dirty = true;
    m_saveTimer.start();
}
