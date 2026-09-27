#include "Hotkey.h"

#include <QCoreApplication>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

constexpr int kId = 0x574E; // "WN"

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
    struct Choice
    {
        UINT modifiers;
        const char *keys;
    };
    // Win+Shift+F first; Windows keeps Win+F for itself and some tools claim
    // the shifted one, so there is a fallback no one else is likely to hold.
    static const Choice choices[] = {
        { MOD_WIN | MOD_SHIFT, "Win+Shift+F" },
        { MOD_CONTROL | MOD_ALT | MOD_SHIFT, "Ctrl+Alt+Shift+F" },
    };
    for (const Choice &choice : choices) {
        if (RegisterHotKey(nullptr, kId, choice.modifiers | MOD_NOREPEAT, 'F')) {
            m_id = kId;
            m_keys = QString::fromLatin1(choice.keys);
            return m_keys;
        }
    }
    return {};
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
