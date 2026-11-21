#pragma once
#include "deviceprofile.h"
#include <QObject>
#include <QVariantList>
#include <memory>
class QLockFile;

// Local device identities, never portable project data or a connection grant.
// Mutations deliberately remain C++-only until the runtime manager guards them.
class DeviceLibrary : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList instances READ instances NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool writable READ writable NOTIFY changed)
  public:
    explicit DeviceLibrary(QString path = {}, QObject *parent = nullptr);
    ~DeviceLibrary() override;
    QVariantList instances() const;
    QVariantMap instance(const QString &id) const;
    std::optional<DeviceProfile> profile(const QString &id) const;
    QString bindingKey(const QString &id) const;
    bool writable() const { return !m_blocked; }
    QString message() const { return m_message; }
    QString add(QString name, const DeviceProfile &profile, QString endpoint = {});
    bool update(const QString &id, QString name, const DeviceProfile &profile, QString endpoint);
    QString duplicate(const QString &id, QString name);
    bool remove(const QString &id);
  signals:
    void changed();

  private:
    struct Entry {
        QString id, name, endpoint;
        DeviceProfile profile;
        int bindingRevision = 1;
    };
    int indexOf(const QString &id) const;
    bool persist(const QList<Entry> &entries);
    bool fail(QString text);
    static std::optional<Entry> validate(QString id, QString name, const DeviceProfile &profile,
                                         QString endpoint, int revision, QString &error);
    static QJsonObject json(const Entry &entry);
    QString m_path, m_message;
    QList<Entry> m_entries;
    std::unique_ptr<QLockFile> m_lock;
    QByteArray m_diskHash;
    bool m_existed = false, m_blocked = false;
};
