#pragma once
#include "artwork.h"
#include "cancellation.h"
#include "deviceprofile.h"
#include <QHash>
#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
struct JobInput {
    QList<Artwork> items;
    QVariantList layers;
    int activePage = 0;
    DeviceProfile deviceProfile = DeviceProfile::m3Reference();
};
class Document;
struct JobMove {
    QPointF from, to;
    double speed = 0, power = 0;
    int layer = 0;
    bool rapid = false;
    int slice = 0;
};
struct JobPlan {
    JobPlan(DeviceProfile profile = DeviceProfile::m3Reference())
        : m_deviceProfile(std::move(profile)) {}
    const DeviceProfile &deviceProfile() const { return m_deviceProfile; }
    QVector<JobMove> moves;
    QHash<QString, QImage> previews;
    bool cancelled = false;
    QStringList errors;
    double workLength = 0, travelLength = 0, seconds = 0, minimumSeconds = 0;
    bool valid() const { return errors.isEmpty() && !moves.isEmpty(); }
    QStringList gcode(const Cancellation &cancel = {}) const;
    void cacheCommands(const Cancellation &cancel = {});
    const QStringList &cachedCommands() const { return m_commands; }
    QVariantMap summary() const;

  private:
    // A command cache and its output configuration cannot be rebound separately.
    DeviceProfile m_deviceProfile;
    QStringList m_commands;
};
class JobPlanner {
  public:
    static JobPlan build(const Document &document);
    static JobPlan build(const JobInput &input, const Cancellation &cancel = {});
    static JobPlan materialTest(double minSpeed, double maxSpeed, double minPower, double maxPower,
                                int steps,
                                const DeviceProfile &profile = DeviceProfile::m3Reference());
    static QPainterPath geometry(const Artwork &item, QString &error, bool fill = false);
};
