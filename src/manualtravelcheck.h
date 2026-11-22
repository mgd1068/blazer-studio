#pragma once
#include "job.h"
#include <limits>
#include <optional>

// Pure planning data. A successful check is neither a measured machine pose nor
// a connection, reference, native-input or physical execution permission.
namespace ManualTravelCheck {
inline constexpr double maximumDistanceMm = 2000.;
struct Distances {
    double negativeX = 0, positiveX = 0, negativeY = 0, positiveY = 0;
};
struct Request {
    std::optional<QPointF> headAnchor;  // Explicit virtual plan coordinates; never assumed 0/0.
    std::optional<Distances> available; // Caller supplies four known free distances in mm.
    double safetyMarginMm = std::numeric_limits<double>::quiet_NaN(); // Explicit, including zero.
    QByteArray expectedSourceFingerprint;
};
class Result {
  public:
    bool valid() const { return m_checked && m_error.isEmpty(); }
    QString error() const { return m_error; }
    bool hasRequirements() const { return m_checked; }
    Distances required() const { return m_required; }
    Distances available() const { return m_available; }
    Distances usableAfterMargin() const { return m_usable; }
    double safetyMarginMm() const { return m_margin; }
    QByteArray sourceFingerprint() const { return m_source; }
    QByteArray requestFingerprint() const { return m_request; }
    bool matches(const JobPlan &plan, const Request &request,
                 const Cancellation &cancel = {}) const;

  private:
    friend Result check(const JobPlan &, const Request &, const Cancellation &);
    bool m_checked = false;
    QString m_error;
    Distances m_required, m_available, m_usable;
    double m_margin = 0;
    QByteArray m_source, m_request;
};
// Empty for invalid, cancelled, unbounded or unsupported source plans. Includes
// immutable output profile and all move fields, not merely a graphics bounding box.
QByteArray sourceFingerprint(const JobPlan &plan, const Cancellation &cancel = {});
Result check(const JobPlan &plan, const Request &request, const Cancellation &cancel = {});
} // namespace ManualTravelCheck
