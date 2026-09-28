#include "Updater.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QVersionNumber>

#define NOMINMAX
#include <windows.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

namespace {

const QUrl kLatest(QStringLiteral("https://api.github.com/repos/Locke-Werks/WormholeNotes/releases/latest"));
constexpr int kFirstCheckMs = 60 * 1000;
constexpr int kEveryMs = 6 * 60 * 60 * 1000;

// Whose installers are run without asking the user to trust them by hand.
bool trustedSigner(const QString &name)
{
    return name.startsWith(QLatin1String("Specter Point Intelligence"), Qt::CaseInsensitive)
        || name.startsWith(QLatin1String("Locke Werks"), Qt::CaseInsensitive);
}

QNetworkRequest request(const QUrl &url)
{
    QNetworkRequest r(url);
    // GitHub refuses API calls without a user agent.
    r.setHeader(QNetworkRequest::UserAgentHeader,
                QStringLiteral("WormholeNotes/%1").arg(QCoreApplication::applicationVersion()));
    r.setRawHeader("Accept", "application/vnd.github+json");
    return r;
}

} // namespace

Updater::Updater(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        check(false);
        m_timer.start(kEveryMs);
    });
}

void Updater::start()
{
    m_timer.start(kFirstCheckMs);
}

void Updater::checkNow()
{
    check(true);
}

std::optional<Updater::Release> Updater::parseRelease(const QByteArray &json)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    QString tag = root.value(QStringLiteral("tag_name")).toString();
    if (tag.startsWith(u'v'))
        tag.remove(0, 1);
    if (QVersionNumber::fromString(tag).isNull() || root.value(QStringLiteral("draft")).toBool()
        || root.value(QStringLiteral("prerelease")).toBool())
        return std::nullopt;
    Release release;
    release.version = tag;
    release.pageUrl = root.value(QStringLiteral("html_url")).toString();
    for (const QJsonValue &asset : root.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject o = asset.toObject();
        if (o.value(QStringLiteral("name")).toString().endsWith(QLatin1String("-Setup.exe"), Qt::CaseInsensitive))
            release.setupUrl = o.value(QStringLiteral("browser_download_url")).toString();
    }
    return release;
}

bool Updater::isNewer(const QString &candidate, const QString &current)
{
    return QVersionNumber::compare(QVersionNumber::fromString(candidate), QVersionNumber::fromString(current)) > 0;
}

bool Updater::isInstalledCopy()
{
    // The installer puts WormholeNotes under Program Files; a copy anywhere
    // else was unzipped or built, and an installer would not replace it.
    const QString programFiles = QDir::fromNativeSeparators(qEnvironmentVariable("ProgramFiles"));
    return !programFiles.isEmpty()
        && QCoreApplication::applicationDirPath().startsWith(programFiles + u'/', Qt::CaseInsensitive);
}

void Updater::check(bool asked)
{
    if (m_busy)
        return;
    m_busy = true;
    QNetworkReply *reply = m_network->get(request(kLatest));
    connect(reply, &QNetworkReply::finished, this, [this, reply, asked] {
        reply->deleteLater();
        m_busy = false;
        if (reply->error() != QNetworkReply::NoError) {
            // A check nobody asked for fails quietly: offline is normal.
            if (asked)
                emit message(tr("Could not check for updates"), reply->errorString(), true);
            return;
        }
        const std::optional<Release> release = parseRelease(reply->readAll());
        if (release && isNewer(release->version, QCoreApplication::applicationVersion())) {
            const bool fresh = !m_available || m_available->version != release->version;
            m_available = release;
            if (fresh || asked)
                emit found(release->version);
            return;
        }
        if (asked)
            emit message(tr("WormholeNotes is up to date"),
                         tr("Version %1 is the latest.").arg(QCoreApplication::applicationVersion()), false);
    });
}

void Updater::install()
{
    if (!m_available || m_busy)
        return;
    const Release release = *m_available;
    if (!isInstalledCopy() || release.setupUrl.isEmpty()) {
        QDesktopServices::openUrl(QUrl(release.pageUrl));
        return;
    }
    m_busy = true;
    const QString setup = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                              .filePath(QStringLiteral("WormholeNotes-%1-Setup.exe").arg(release.version));
    QNetworkReply *reply = m_network->get(request(QUrl(release.setupUrl)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, setup, release] {
        reply->deleteLater();
        m_busy = false;
        if (reply->error() != QNetworkReply::NoError) {
            emit message(tr("Could not download the update"), reply->errorString(), true);
            return;
        }
        QFile file(setup);
        if (!file.open(QIODevice::WriteOnly) || file.write(reply->readAll()) < 0) {
            emit message(tr("Could not save the update"), file.errorString(), true);
            return;
        }
        file.close();
        QString signer;
        if (!signedBy(setup, &signer) || !trustedSigner(signer)) {
            // Not ours, or not signed at all: never run it, and let the user
            // look at the release themselves.
            QFile::remove(setup);
            emit message(tr("Update not installed"),
                         signer.isEmpty() ? tr("The installer is not signed. Opening the release page instead.")
                                          : tr("The installer is signed by %1, not Locke Werks. Opening the release page instead.").arg(signer),
                         true);
            QDesktopServices::openUrl(QUrl(release.pageUrl));
            return;
        }
        runInstaller(setup);
    });
}

void Updater::runInstaller(const QString &setup)
{
    // The installer cannot replace a running WormholeNotes, so this copy
    // quits, and a shell it leaves behind waits for the installer and starts
    // the installed copy again. The installer's own UAC prompt comes from
    // start, which goes through ShellExecute.
    const QString self = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    QProcess shell;
    shell.setProgram(QStringLiteral("cmd.exe"));
    shell.setNativeArguments(QStringLiteral("/c start \"\" /wait \"%1\" & start \"\" \"%2\"")
                                 .arg(QDir::toNativeSeparators(setup), self));
    shell.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
    if (!shell.startDetached()) {
        emit message(tr("Could not start the installer"), shell.errorString(), true);
        return;
    }
    QCoreApplication::quit();
}

bool Updater::signedBy(const QString &file, QString *signer)
{
    signer->clear();
    const std::wstring path = QDir::toNativeSeparators(file).toStdWString();
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof fileInfo;
    fileInfo.pcwszFilePath = path.c_str();
    WINTRUST_DATA data{};
    data.cbStruct = sizeof data;
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &fileInfo;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);

    if (status == ERROR_SUCCESS) {
        if (CRYPT_PROVIDER_DATA *provider = WTHelperProvDataFromStateData(data.hWVTStateData)) {
            if (CRYPT_PROVIDER_SGNR *sgnr = WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0)) {
                if (CRYPT_PROVIDER_CERT *cert = WTHelperGetProvCertFromChain(sgnr, 0)) {
                    wchar_t name[256] = {};
                    CertGetNameStringW(cert->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 256);
                    *signer = QString::fromWCharArray(name);
                }
            }
        }
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    return status == ERROR_SUCCESS;
}
