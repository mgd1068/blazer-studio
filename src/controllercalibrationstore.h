#pragma once
#include <QByteArray>
#include <QString>
#include <QVariantMap>

// Private local calibration DATA only. Authenticity does not grant a connection,
// machine reference or permission to execute. The worker validates its schema
// and current device/settings/readback before using any restored data.
class ControllerCalibrationStore final {
  public:
    static constexpr qint64 maximumFileBytes = 64 * 1024;
    explicit ControllerCalibrationStore(QString directory = {});
    // Binding is exactly 64 lowercase SHA256 hex characters, including the local
    // device instance identity. A missing record returns {} without an error.
    QVariantMap load(QByteArray binding, QString *error = nullptr) const;
    bool save(QByteArray binding, QVariantMap record, QString *error = nullptr);

  private:
    QString m_directory;
};
