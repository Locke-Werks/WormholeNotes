#include "NativeHost.h"

#include "BrowserReader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
// extension runs in Chrome, Edge and Brave and each is its own place.
QString parentExe()
{
    const DWORD self = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return {};
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof entry;
    DWORD parent = 0;
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry)) {
        if (entry.th32ProcessID == self) {
            parent = entry.th32ParentProcessID;
            break;
        }
    }
    QString exe;
    entry.dwSize = sizeof entry;
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok && parent; ok = Process32NextW(snapshot, &entry)) {
        if (entry.th32ProcessID == parent) {
            exe = QString::fromWCharArray(entry.szExeFile).toLower();
            break;
        }
    }
    CloseHandle(snapshot);
    return exe;
}

bool readExactly(char *buffer, size_t size)
{
    return size == 0 || fread(buffer, 1, size, stdin) == size;
}

} // namespace

namespace NativeHost {

bool isHostLaunch(int argc, char *argv[])
{
    return argc > 1 && qstrncmp(argv[1], "chrome-extension://", 19) == 0;
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

void registerForUser()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    const QString manifestPath = QDir::toNativeSeparators(dir + QStringLiteral("/native-host.json"));

    QJsonObject manifest;
    manifest.insert(QStringLiteral("name"), QString::fromLatin1(kHostName));
    manifest.insert(QStringLiteral("description"), QStringLiteral("WormholeNotes page detection"));
    manifest.insert(QStringLiteral("path"), QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
    manifest.insert(QStringLiteral("type"), QStringLiteral("stdio"));
    manifest.insert(QStringLiteral("allowed_origins"),
                    QJsonArray{ QStringLiteral("chrome-extension://%1/").arg(QString::fromLatin1(kExtensionId)) });
    const QByteArray bytes = QJsonDocument(manifest).toJson();

    QFile existing(manifestPath);
    if (!existing.open(QIODevice::ReadOnly) || existing.readAll() != bytes) {
        existing.close();
        QSaveFile file(manifestPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(bytes);
            file.commit();
        }
    }

    static const wchar_t *const browsers[] = {
        L"Software\\Google\\Chrome\\NativeMessagingHosts\\",
        L"Software\\Microsoft\\Edge\\NativeMessagingHosts\\",
        L"Software\\BraveSoftware\\Brave-Browser\\NativeMessagingHosts\\",
    };
    const std::wstring value = manifestPath.toStdWString();
    for (const wchar_t *base : browsers) {
        const std::wstring key = std::wstring(base) + QString::fromLatin1(kHostName).toStdWString();
        RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), nullptr, REG_SZ, value.c_str(),
                        DWORD((value.size() + 1) * sizeof(wchar_t)));
    }
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
    const auto pages = [](const QJsonValue &value) {
        QList<ExtensionReport::Page> out;
        for (const QJsonValue &tab : value.toArray()) {
            const QJsonObject o = tab.toObject();
            out.append({ BrowserReader::pageOf(o.value(QStringLiteral("url")).toString()),
                         o.value(QStringLiteral("title")).toString() });
        }
        return out;
    };
    report.showing = pages(message.value(QStringLiteral("showing")));
    report.tabs = pages(message.value(QStringLiteral("tabs")));
    emit reported(report);
}
