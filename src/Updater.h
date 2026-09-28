#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <optional>

class QNetworkAccessManager;

// Asks GitHub now and then whether a newer release is out, and fetches it
// when asked to. Only the latest release's public metadata is requested;
// nothing about the notes leaves the machine.
class Updater : public QObject
{
    Q_OBJECT

public:
    struct Release
    {
        QString version;  // "0.2.0", no leading v
        QString setupUrl; // the Setup.exe asset, empty when there is none
        QString pageUrl;  // the release's page on GitHub
    };

    explicit Updater(QObject *parent = nullptr);

    // First check a minute after start, then every six hours.
    void start();
    // A check the user asked for: reports "up to date" too.
    void checkNow();
    const std::optional<Release> &available() const { return m_available; }
    // An installed copy downloads the installer, checks its signature, runs
    // it and restarts once it closes. Any other copy opens the release page.
    void install();

    static std::optional<Release> parseRelease(const QByteArray &json);
    static bool isNewer(const QString &candidate, const QString &current);
    // True when the file carries a valid Authenticode signature; the
    // signer's name is written to `signer`.
    static bool signedBy(const QString &file, QString *signer);
    static bool isInstalledCopy();

Q_SIGNALS:
    void found(const QString &version);
    // The outcome of a check the user asked for that found nothing, or of
    // an update that could not go ahead.
    void message(const QString &title, const QString &text, bool warning);

private:
    void check(bool asked);
    void runInstaller(const QString &setup);

    QNetworkAccessManager *m_network = nullptr;
    QTimer m_timer;
    std::optional<Release> m_available;
    bool m_busy = false;
};
