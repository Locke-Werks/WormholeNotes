#include "NativeHost.h"

#include "BrowserReader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSaveFile>
#include <QStandardPaths>

#include <cstdio>
#include <fcntl.h>
#include <io.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

namespace {

const QString kSocket = QStringLiteral("LockeWerks.WormholeNotes.Pages");

// The browser that started us, by its executable name, since the same
// extension runs in Chrome, Edge, Brave and Firefox and each is its own place.
// On Windows a Chromium browser starts a host through cmd.exe, so the shell in
// between is stepped over.
QString parentExe()
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return {};
    struct Process
    {
        DWORD parent = 0;
        QString exe;
    };
    QHash<DWORD, Process> processes;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof entry;
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry))
        processes.insert(entry.th32ProcessID,
                         { entry.th32ParentProcessID, QString::fromWCharArray(entry.szExeFile).toLower() });
    CloseHandle(snapshot);

    DWORD pid = processes.value(GetCurrentProcessId()).parent;
    for (int depth = 0; depth < 4 && processes.contains(pid); ++depth) {
        const Process &process = processes[pid];
        if (process.exe != QLatin1String("cmd.exe") && process.exe != QLatin1String("conhost.exe"))
            return process.exe;
        pid = process.parent;
    }
    return {};
}

bool readExactly(char *buffer, size_t size)
{
    return size == 0 || fread(buffer, 1, size, stdin) == size;
}

} // namespace

namespace NativeHost {

bool isHostLaunch(int argc, char *argv[])
{
    if (argc > 1 && qstrncmp(argv[1], "chrome-extension://", 19) == 0)
        return true;
    return argc > 2 && qstrcmp(argv[2], kGeckoId) == 0;
}

int run()
{
    // Native messaging is length-prefixed binary on stdin and stdout; text
    // mode would turn a 0x0a in a length into two bytes.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    const QString browser = parentExe();
    QLocalSocket socket;
    for (;;) {
        quint32 length = 0;
        if (!readExactly(reinterpret_cast<char *>(&length), sizeof length))
            return 0;
        // The protocol caps a message from the browser at 64 MiB; anything
        // bigger is not a message.
        if (length > 64u * 1024 * 1024)
            return 1;
        QByteArray body(qsizetype(length), Qt::Uninitialized);
        if (!readExactly(body.data(), length))
            return 0;

        QJsonObject message = QJsonDocument::fromJson(body).object();
        message.insert(QStringLiteral("browser"), browser);
        // Every browser profile runs its own copy of the extension, and each
        // copy its own host: this process's id tells the profiles apart.
        message.insert(QStringLiteral("source"), qint64(GetCurrentProcessId()));
        if (socket.state() != QLocalSocket::ConnectedState) {
            socket.connectToServer(kSocket);
            if (!socket.waitForConnected(500))
                continue; // WormholeNotes is not running; the next report may find it.
        }
        socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
        if (!socket.waitForBytesWritten(500))
            socket.abort();
    }
}

// Writes a host manifest, only when it differs, and points each registry
// base at it.
template <typename Bases>
static void registerManifest(const QString &path, const QJsonObject &manifest, const Bases &bases)
{
    const QByteArray bytes = QJsonDocument(manifest).toJson();
    QFile existing(path);
    if (!existing.open(QIODevice::ReadOnly) || existing.readAll() != bytes) {
        existing.close();
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(bytes);
            file.commit();
        }
    }
    const std::wstring value = path.toStdWString();
    for (const wchar_t *base : bases) {
        const std::wstring key = std::wstring(base) + QString::fromLatin1(kHostName).toStdWString();
        RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), nullptr, REG_SZ, value.c_str(),
                        DWORD((value.size() + 1) * sizeof(wchar_t)));
    }
}

namespace {

const wchar_t *const kChromiumBases[] = {
    L"Software\\Google\\Chrome\\NativeMessagingHosts\\",
    L"Software\\Microsoft\\Edge\\NativeMessagingHosts\\",
    L"Software\\BraveSoftware\\Brave-Browser\\NativeMessagingHosts\\",
};
const wchar_t *const kFirefoxBases[] = { L"Software\\Mozilla\\NativeMessagingHosts\\" };

QString manifestDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

} // namespace

void unregisterForUser()
{
    const QString self = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const auto drop = [&](const QString &name, const auto &bases) {
        const QString path = manifestDir() + u'/' + name;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return;
        const QString named = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("path")).toString();
        file.close();
        if (named.compare(self, Qt::CaseInsensitive) != 0)
            return;
        for (const wchar_t *base : bases) {
            const std::wstring key = std::wstring(base) + QString::fromLatin1(kHostName).toStdWString();
            RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str());
        }
        QFile::remove(path);
    };
    drop(QStringLiteral("native-host.json"), kChromiumBases);
    drop(QStringLiteral("native-host-firefox.json"), kFirefoxBases);
}

void registerForUser()
{
    const QString dir = manifestDir();
    QDir().mkpath(dir);

    QJsonObject manifest;
    manifest.insert(QStringLiteral("name"), QString::fromLatin1(kHostName));
    manifest.insert(QStringLiteral("description"), QStringLiteral("WormholeNotes page detection"));
    manifest.insert(QStringLiteral("path"), QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
    manifest.insert(QStringLiteral("type"), QStringLiteral("stdio"));

    // Chromium names the callers it allows by origin and Firefox by add-on
    // ID, and each rejects the other's key, so they get a manifest apiece.
    QJsonObject chromium = manifest;
    chromium.insert(QStringLiteral("allowed_origins"),
                    QJsonArray{ QStringLiteral("chrome-extension://%1/").arg(QString::fromLatin1(kExtensionId)) });
    registerManifest(QDir::toNativeSeparators(dir + QStringLiteral("/native-host.json")), chromium, kChromiumBases);

    QJsonObject firefox = manifest;
    firefox.insert(QStringLiteral("allowed_extensions"), QJsonArray{ QString::fromLatin1(kGeckoId) });
    registerManifest(QDir::toNativeSeparators(dir + QStringLiteral("/native-host-firefox.json")), firefox, kFirefoxBases);
}

} // namespace NativeHost

ExtensionLink::ExtensionLink(QObject *parent)
    : QObject(parent)
    , m_server(new QLocalServer(this))
{
    // Only this user's processes may connect.
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                while (socket->canReadLine())
                    onMessage(QJsonDocument::fromJson(socket->readLine()).object());
            });
        }
    });
}

bool ExtensionLink::listen()
{
    QLocalServer::removeServer(kSocket);
    return m_server->listen(kSocket);
}

void ExtensionLink::onMessage(const QJsonObject &message)
{
    ExtensionReport report;
    report.browser = message.value(QStringLiteral("browser")).toString();
    if (report.browser.isEmpty())
        return;
    report.source = message.value(QStringLiteral("source")).toInteger();
    const auto pages = [](const QJsonValue &value) {
        QList<ExtensionReport::Page> out;
        for (const QJsonValue &tab : value.toArray()) {
            const QJsonObject o = tab.toObject();
            out.append({ BrowserReader::pageOf(o.value(QStringLiteral("url")).toString()),
                         o.value(QStringLiteral("title")).toString(),
                         o.value(QStringLiteral("window")).toInteger(-1) });
        }
        return out;
    };
    report.showing = pages(message.value(QStringLiteral("showing")));
    report.tabs = pages(message.value(QStringLiteral("tabs")));
    emit reported(report);
}
