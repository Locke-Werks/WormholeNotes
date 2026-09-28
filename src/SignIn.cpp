#include "SignIn.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

namespace {

const QString kRun = QStringLiteral("HKEY_%1\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
const QString kName = QStringLiteral("WormholeNotes");
const QString kSetting = QStringLiteral("signIn");

QString self()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

bool names(const QString &command, const QString &exe)
{
    return command.contains(exe, Qt::CaseInsensitive);
}

// The installer's entry, when it is there and starts this copy.
bool machineEntry()
{
    const QSettings machine(kRun.arg(QStringLiteral("LOCAL_MACHINE")), QSettings::NativeFormat);
    return names(machine.value(kName).toString(), self());
}

} // namespace

namespace SignIn {

bool enabled()
{
    const QSettings settings;
    if (settings.contains(kSetting))
        return settings.value(kSetting).toBool();
    const QSettings user(kRun.arg(QStringLiteral("CURRENT_USER")), QSettings::NativeFormat);
    return machineEntry() || names(user.value(kName).toString(), self());
}

void setEnabled(bool on)
{
    QSettings().setValue(kSetting, on);
    QSettings user(kRun.arg(QStringLiteral("CURRENT_USER")), QSettings::NativeFormat);
    // With the installer's entry there, only the setting needs to change.
    if (on && !machineEntry())
        user.setValue(kName, QStringLiteral("\"%1\" %2").arg(self(), QLatin1String(kArgument)));
    else if (!on && names(user.value(kName).toString(), self()))
        user.remove(kName);
}

bool shouldQuit(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], kArgument) == 0)
            return !QSettings().value(kSetting, true).toBool();
    }
    return false;
}

void forget()
{
    QSettings user(kRun.arg(QStringLiteral("CURRENT_USER")), QSettings::NativeFormat);
    if (names(user.value(kName).toString(), self()))
        user.remove(kName);
}

} // namespace SignIn
