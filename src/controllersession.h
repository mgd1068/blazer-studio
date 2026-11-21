#pragma once
#include "deviceprofile.h"
#include <QObject>
#include <QPointF>
#include <QStringList>
#include <QVariantMap>
#include <memory>
struct JobPlan;
struct ControllerSubmission {
    quint64 requestId = 0;
    bool enqueued = false;
    QString reason;
    explicit operator bool() const { return enqueued; }
};

// A session owns live state; output artifacts and profiles never imply readiness.
struct ControllerStatus {
    bool connected = false, ready = false, busy = false, executing = false;
    bool paused = false, firing = false;
    double progress = 0;
    QPointF position;
    QString state = "Getrennt";
    bool verified = false;
    bool positionReferenced = false;
    QString motionSafetyError;
    QString homingSafetyError;
    bool referencing = false, resetting = false;
    bool overridesReady = false;
    QString overridesSafetyError = "Echtzeit-Overrides sind für diese Sitzung nicht freigegeben.";
    // A bounded non-motion work-coordinate transaction, never a lease exemption.
    bool coordinateUpdatePending = false;
    // Runtime transport state, never a physical permission or refreshed by GUI receipt.
    bool opening = false, closing = false, requestPending = false, occupied = false;
    bool statusFresh = false;
    qint64 statusAgeMs = -1, runtimeHeartbeatAgeMs = -1;
    quint64 connectionEpoch = 0, sequence = 0;
    QString connectionPhase = "closed";
    bool accessoryLeaseActive = false, accessoryJobActive = false, accessoryFocusActive = false;
    QString extractionSafetyError{};
    quint64 coordinateEpoch = 0; // Reference/WCO/reset identity; never motion authority.
};
class ControllerSession : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    ~ControllerSession() override = default;
    virtual const DeviceProfile &deviceProfile() const = 0;
    virtual ControllerStatus status() const = 0;
    virtual QStringList logEntries() const = 0;
    virtual QVariantMap diagnosticSnapshot() const { return {{"supported", false}}; }
    virtual bool open(const QString &endpoint) = 0;
    // Async runtime admission. Enqueued does not mean port-open or verified.
    virtual ControllerSubmission requestOpen(const QString &endpoint,
                                              const QByteArray &bindingDigest) {
        Q_UNUSED(endpoint);
        Q_UNUSED(bindingDigest);
        return {0, false, "Asynchrone Verbindungsanforderung nicht unterstützt."};
    }
    virtual void close() = 0;
    virtual void start(const JobPlan &plan) = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void jog(double dx, double dy) = 0;
    virtual void home() = 0;
    virtual void zero() = 0;
    virtual void focus(bool on) = 0;
    virtual void frame(const QVariantMap &bounds) = 0;
    virtual bool overrides(int feed, int power) = 0;
  signals:
    void changed();
    void connectionRequestFinished(quint64 requestId, bool opened, const QString &reason);
};
namespace ControllerSessions {
// Construction does not enumerate/open endpoints or grant hardware access.
std::unique_ptr<ControllerSession> create(const DeviceProfile &profile, QString &error);
QStringList availableEndpoints(const DeviceProfile &profile);
} // namespace ControllerSessions
