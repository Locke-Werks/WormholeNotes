#include "Hotkey.h"

#include <QCoreApplication>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

constexpr int kId = 0x574E; // "WN"

struct Choice
{
    UINT modifiers;
    const char *keys;
};
// Win+Shift+F first; Windows keeps Win+F for itself and some tools claim the
// shifted one, so there are fallbacks no one else is likely to hold.
const Choice kChoices[] = {
    { MOD_WIN | MOD_SHIFT, "Win+Shift+F" },
    { MOD_CONTROL | MOD_ALT | MOD_SHIFT, "Ctrl+Alt+Shift+F" },
    { MOD_WIN | MOD_ALT, "Win+Alt+F" },
};

} // namespace

Hotkey::Hotkey(QObject *parent)
    : QObject(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

Hotkey::~Hotkey()
{
    if (m_id)
        UnregisterHotKey(nullptr, m_id);
    QCoreApplication::instance()->removeNativeEventFilter(this);
}

QString Hotkey::registerFirst()
{
    for (const Choice &choice : kChoices) {
        if (registerKeys(QString::fromLatin1(choice.keys)))
            return m_keys;
    }
    return {};
}

bool Hotkey::registerKeys(const QString &keys)
{
    if (m_id) {
        UnregisterHotKey(nullptr, m_id);
        m_id = 0;
    }
    m_keys.clear();
    if (keys.isEmpty())
        return true;
    for (const Choice &choice : kChoices) {
        if (keys == QLatin1String(choice.keys) && RegisterHotKey(nullptr, kId, choice.modifiers | MOD_NOREPEAT, 'F')) {
            m_id = kId;
            m_keys = keys;
            return true;
        }
    }
    return false;
}

QStringList Hotkey::choices()
{
    QStringList out;
    for (const Choice &choice : kChoices)
        out.append(QString::fromLatin1(choice.keys));
    return out;
}

bool Hotkey::nativeEventFilter(const QByteArray &, void *message, qintptr *)
{
    const auto *msg = static_cast<const MSG *>(message);
    if (msg->message == WM_HOTKEY && m_id && int(msg->wParam) == m_id) {
        emit pressed();
        return true;
    }
    return false;
}
