#pragma once
#include "job.h"
#include "jobplacement.h"
#include <QTransform>

// Offline two-target output registration. Coordinates are explicit data, never
// evidence of a real reference, physical measurement or permission to execute.
namespace ManualRegistration {
enum class Scaling { Rigid, Uniform };
struct Parameters {
    QPointF designFirst, designSecond, targetFirst, targetSecond;
    Scaling scaling = Scaling::Rigid;
    double maximumResidualMm = .5;
};
QByteArray geometryFingerprint(const JobInput &input, const Cancellation &cancel = {});
class SourceSnapshot {
  public:
    static SourceSnapshot capture(const JobInput &input, const JobPlan &plan,
                                  const QByteArray &offlineFrame,
                                  const Cancellation &cancel = {});
    bool valid() const { return m_error.isEmpty() && !m_fingerprint.isEmpty(); }
    QString error() const { return m_error; }
    QByteArray fingerprint() const { return m_fingerprint; }
    QByteArray geometry() const { return m_geometry; }
    bool matches(const JobInput &input, const JobPlan &plan, const QByteArray &offlineFrame,
                 const Cancellation &cancel = {}) const;
  private:
    QByteArray m_fingerprint, m_geometry, m_motion, m_profile, m_frame;
    QString m_error;
};
struct Intent { SourceSnapshot source; Parameters parameters; };
QByteArray intentFingerprint(const Intent &intent);
struct Context {
    std::optional<Intent> intent;
    QByteArray offlineFrame;
};
struct Result {
    JobPlan plan;
    QVariantMap summary;
    QTransform transform;
    bool valid() const { return plan.valid() && summary.value("valid").toBool(); }
};
Result apply(const JobInput &input, const JobPlan &canonical, const Intent &intent,
             const QByteArray &offlineFrame,
             const std::optional<JobPlacement::PositionSnapshot> &approach = {},
             const Cancellation &cancel = {});
} // namespace ManualRegistration
