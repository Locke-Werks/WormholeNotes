// Updater: reading GitHub's latest-release answer, comparing versions, and
// telling a signed file from an unsigned one.

#include "../src/Updater.h"

#include <QGuiApplication>
#include <QLibraryInfo>
#include <QTextStream>

static int failures = 0;

static void check(bool ok, const QString &what)
{
    QTextStream(stdout) << (ok ? "ok   " : "FAIL ") << what << Qt::endl;
    if (!ok)
        ++failures;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    const QByteArray latest = R"({
        "tag_name": "v0.2.0", "draft": false, "prerelease": false,
        "html_url": "https://github.com/Locke-Werks/WormholeNotes/releases/tag/v0.2.0",
        "assets": [
            { "name": "WormholeNotes-0.2.0-portable-x64.zip", "browser_download_url": "https://example.test/zip" },
            { "name": "WormholeNotes-0.2.0-Setup.exe", "browser_download_url": "https://example.test/setup" }
        ] })";
    const auto release = Updater::parseRelease(latest);
    check(release && release->version == QLatin1String("0.2.0"), "the tag gives the version, without its v");
    check(release && release->setupUrl == QLatin1String("https://example.test/setup"), "the Setup.exe asset is found");
    check(release && release->pageUrl.endsWith(QLatin1String("/v0.2.0")), "the release page is kept");
    check(!Updater::parseRelease(R"({ "tag_name": "v0.3.0", "prerelease": true })"), "a prerelease is not offered");
    check(!Updater::parseRelease("not json"), "a broken answer is no release");

    check(Updater::isNewer(QStringLiteral("0.2.0"), QStringLiteral("0.1.0")), "0.2.0 is newer than 0.1.0");
    check(Updater::isNewer(QStringLiteral("0.10.0"), QStringLiteral("0.9.1")), "versions compare by number, not text");
    check(!Updater::isNewer(QStringLiteral("0.1.0"), QStringLiteral("0.1.0")), "the same version is not newer");
    check(!Updater::isNewer(QStringLiteral("0.1.0"), QStringLiteral("0.2.0")), "an older version is not newer");

    QString signer;
    const QString qtCore = QLibraryInfo::path(QLibraryInfo::BinariesPath) + QStringLiteral("/Qt6Core.dll");
    const bool qtSigned = Updater::signedBy(qtCore, &signer);
    check(qtSigned && signer.contains(QLatin1String("Qt")), QStringLiteral("Qt6Core.dll is signed (%1)").arg(signer));
    check(!Updater::signedBy(QCoreApplication::applicationFilePath(), &signer) && signer.isEmpty(),
          "this unsigned test is not");
    return failures == 0 ? 0 : 1;
}
