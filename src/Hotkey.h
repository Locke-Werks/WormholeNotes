#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QString>
#include <QStringList>

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
    // Swaps to one combination by how it is written; empty turns the hotkey
    // off. False, with no hotkey held, when another program has it.
    bool registerKeys(const QString &keys);
    QString keys() const { return m_keys; }
    // Every combination on offer, as written.
    static QStringList choices();

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

Q_SIGNALS:
    void pressed();

private:
    int m_id = 0;
    QString m_keys;
};
