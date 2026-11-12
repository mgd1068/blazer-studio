#pragma once
#include "cancellation.h"
#include "deviceprofile.h"
#include <QStringList>
struct JobPlan;

// An output artifact is data, never a connection or an execution grant.
// Binary adapters can return their own media type without becoming GCode.
class ControllerProgram {
  public:
    static ControllerProgram success(const DeviceProfile &profile, QByteArray sourceFingerprint,
                                     QString mediaType, QByteArray payload);
    static ControllerProgram failure(const DeviceProfile &profile, QByteArray sourceFingerprint,
                                     QString error, bool cancelled = false);
    bool valid() const { return !m_cancelled && m_errors.isEmpty() && !m_payload.isEmpty(); }
    bool cancelled() const { return m_cancelled; }
    QString driverId() const { return m_driverId; }
    QString transport() const { return m_transport; }
    QString mediaType() const { return m_mediaType; }
    const QByteArray &payload() const { return m_payload; }
    const QByteArray &profileFingerprint() const { return m_profileFingerprint; }
    const QByteArray &sourceFingerprint() const { return m_sourceFingerprint; }
    const QStringList &errors() const { return m_errors; }
    bool matches(const DeviceProfile &profile, const QByteArray &source) const;
    QStringList gcodeLines() const;

  private:
    ControllerProgram(const DeviceProfile &profile, QByteArray source);
    QString m_driverId, m_transport, m_mediaType;
    QByteArray m_payload, m_profileFingerprint, m_sourceFingerprint;
    QStringList m_errors;
    bool m_cancelled = false;
};

class ControllerAdapter {
  public:
    virtual ~ControllerAdapter() = default;
    virtual QString outputError(const DeviceProfile &profile) const = 0;
    virtual std::optional<DeviceProfile> projectProfile(const DeviceProfile &profile,
                                                        QString &error) const = 0;
    virtual ControllerProgram encode(const JobPlan &plan, const QByteArray &sourceFingerprint,
                                     const Cancellation &cancel) const = 0;
};

namespace ControllerAdapters {
// Statically registered, stateless adapters. Unknown IDs/transports have no fallback.
const ControllerAdapter *find(const DeviceProfile &profile);
QString outputError(const DeviceProfile &profile);
std::optional<DeviceProfile> projectProfile(const DeviceProfile &profile, QString &error);
} // namespace ControllerAdapters
