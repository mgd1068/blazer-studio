#pragma once
#include "devicelibrary.h"
#include <QObject>
#include <QUrl>
class Document;
class Machine;

// The only QML mutation/connection facade for local physical device identities.
// Selecting an instance never opens an endpoint or grants hardware acceptance.
class DeviceManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList instances READ instances NOTIFY changed)
    Q_PROPERTY(QVariantMap active READ active NOTIFY changed)
    Q_PROPERTY(QString activeId READ activeId NOTIFY changed)
    Q_PROPERTY(QString bindingKey READ bindingKey NOTIFY changed)
    Q_PROPERTY(QString parameterHash READ parameterHash NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool connectionRequestPending READ connectionRequestPending NOTIFY changed)
    Q_PROPERTY(QString connectionStatusText READ connectionStatusText NOTIFY changed)
    Q_PROPERTY(QString changeBlockReason READ changeBlockReason NOTIFY changed)
    Q_PROPERTY(QStringList endpoints READ endpoints NOTIFY changed)
  public:
    explicit DeviceManager(Document *document, Machine *machine, QString path = {},
                           QObject *parent = nullptr);
    QVariantList instances() const { return library.instances(); }
    QVariantMap active() const;
    QString activeId() const { return bindingKey().isEmpty() ? QString() : selectedId; }
    QString bindingKey() const;
    QString parameterHash() const;
    QString message() const { return lastMessage; }
    bool connectionRequestPending() const { return pendingConnectionRequestId != 0; }
    QString connectionStatusText() const;
    QString changeBlockReason() const;
    QStringList endpoints() const { return discoveredEndpoints; }
    Q_INVOKABLE void refreshEndpoints();
    Q_INVOKABLE QVariantMap details(const QString &id) const { return library.instance(id); }
    Q_INVOKABLE QString addCurrent(QString name, QString endpoint);
    Q_INVOKABLE QString importProfile(const QUrl &url, QString name, QString endpoint);
    Q_INVOKABLE bool activate(const QString &id);
    Q_INVOKABLE bool update(const QString &id, QString name, QString endpoint);
    Q_INVOKABLE QString duplicate(const QString &id, QString name);
    Q_INVOKABLE bool remove(const QString &id);
    // True means local queue admission only; port-open/verification complete asynchronously.
    // False guarantees that no new connection request was enqueued by this call.
    Q_INVOKABLE bool connectSelected();
  signals:
    void changed();
  private:
    bool allowChange();
    bool fail(QString text);
    void invalidateSelection();
    Document *document;
    Machine *machine;
    DeviceLibrary library;
    QString selectedId, selectedBinding, lastMessage;
    quint64 pendingConnectionRequestId = 0;
    QString pendingConnectionBinding;
    QStringList discoveredEndpoints;
    bool activating = false;
    bool changing = false;
    struct Mutation {
        DeviceManager &owner;
        explicit Mutation(DeviceManager &value) : owner(value) { owner.changing = true; }
        ~Mutation() { owner.changing = false; emit owner.changed(); }
    };
};
