#pragma once
#include "artwork.h"
#include "cancellation.h"
#include "controlleradapter.h"
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
// Read-only element access prevents retained mutable references from bypassing
// Qt copy-on-write and changing a plan after its program has been cached.
class JobMoves {
  public:
    JobMoves() = default;
    JobMoves(std::initializer_list<JobMove> values) : m_values(values) {}
    JobMoves &operator=(std::initializer_list<JobMove> values) {
        m_values = values;
        return *this;
    }
    qsizetype size() const { return m_values.size(); }
    bool isEmpty() const { return m_values.isEmpty(); }
    const JobMove &operator[](qsizetype index) const { return m_values[index]; }
    const JobMove &first() const { return m_values.first(); }
    const JobMove &last() const { return m_values.last(); }
    const JobMove *constData() const { return m_values.constData(); }
    auto begin() const { return m_values.cbegin(); }
    auto end() const { return m_values.cend(); }
    const QVector<JobMove> &values() const { return m_values; }
    void append(JobMove move) { m_values.append(std::move(move)); }
    void replace(qsizetype index, JobMove move) { m_values.replace(index, std::move(move)); }
    void clear() { m_values.clear(); }
    void resize(qsizetype size) { m_values.resize(size); }
    void reserve(qsizetype size) { m_values.reserve(size); }

  private:
    QVector<JobMove> m_values;
};
struct JobPlan {
    static constexpr int maximumMoves = 500000;
    JobPlan(DeviceProfile profile = DeviceProfile::m3Reference())
        : m_deviceProfile(std::move(profile)) {}
    const DeviceProfile &deviceProfile() const { return m_deviceProfile; }
    JobMoves moves;
    QHash<QString, QImage> previews;
    bool cancelled = false;
    QStringList errors;
    double workLength = 0, travelLength = 0, seconds = 0, minimumSeconds = 0;
    bool valid() const { return !cancelled && errors.isEmpty() && !moves.isEmpty(); }
    QByteArray motionFingerprint(const Cancellation &cancel = {}) const;
    ControllerProgram encodedProgram(const Cancellation &cancel = {}) const;
    QStringList gcode(const Cancellation &cancel = {}) const;
    void cacheCommands(const Cancellation &cancel = {});
    QStringList cachedCommands() const;
    QVariantMap summary() const;

  private:
    // A command cache and its output configuration cannot be rebound separately.
    DeviceProfile m_deviceProfile;
    std::optional<ControllerProgram> m_program;
    // Qt value sharing keeps the published, read-only plan cheap to inspect.
    // A normal write detaches moves from this cached value and forces rebinding.
    QVector<JobMove> m_programMoves;
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
