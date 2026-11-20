#pragma once
#include "deviceprofile.h"
#include <QObject>
#include <QPointF>
#include <QStringList>
#include <QVariantMap>
#include <memory>
struct JobPlan;

// A session owns live state; output artifacts and profiles never imply readiness.
struct ControllerStatus {
    bool connected = false, ready = false, busy = false, executing = false;
    bool paused = false, firing = false;
    double progress = 0;
    QPointF position;
    QString state = "Getrennt";
    bool verified = false;
};
class ControllerSession : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    ~ControllerSession() override = default;
    virtual const DeviceProfile &deviceProfile() const = 0;
    virtual ControllerStatus status() const = 0;
    virtual QStringList logEntries() const = 0;
    virtual bool open(const QString &endpoint) = 0;
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
};
namespace ControllerSessions {
// Construction does not enumerate/open endpoints or grant hardware access.
std::unique_ptr<ControllerSession> create(const DeviceProfile &profile, QString &error);
QStringList availableEndpoints(const DeviceProfile &profile);
} // namespace ControllerSessions
