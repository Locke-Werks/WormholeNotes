#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QString>

// A key combination that reaches WormholeNotes from any app. Windows hands a
// registered hotkey to the thread that registered it, so it is caught on its
// way through Qt's event loop.
class Hotkey : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    explicit Hotkey(QObject *parent = nullptr);
    ~Hotkey() override;

    // Tries each combination in turn and keeps the first Windows grants.
    // Returns how the one it got is written, or empty when none was free.
    QString registerFirst();
    QString keys() const { return m_keys; }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

Q_SIGNALS:
    void pressed();

private:
    int m_id = 0;
    QString m_keys;
};
