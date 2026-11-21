#pragma once
#include <QObject>
#include <QVariantList>
#include <memory>
class QLockFile;

// User observations only. A matching historical record is never an execution,
// material-proof, calibration, hardware, motion, or extraction grant.
class MaterialTestLedger : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList records READ records NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool writable READ writable NOTIFY changed)
  public:
    explicit MaterialTestLedger(QString path = {}, QObject *parent = nullptr);
    ~MaterialTestLedger() override;
    QVariantList records() const { return entries; }
    QString message() const { return lastMessage; }
    bool writable() const { return !blocked; }
    Q_INVOKABLE QVariantList applicable(const QString &bindingKey,
                                        const QString &parameterHash) const;
    Q_INVOKABLE QString record(const QString &bindingKey, const QString &parameterHash,
                               const QVariantMap &observation);
  signals:
    void changed();

  private:
    bool fail(QString message, bool protect = false);
    bool persist(const QVariantList &next);
    QString path, lastMessage;
    QVariantList entries;
    std::unique_ptr<QLockFile> lock;
    QByteArray diskHash;
    bool existed = false, blocked = false;
};
