#pragma once
#include "job.h"
#include <optional>

// Pure, offline placement of an existing canonical-mm movement plan. None of
// these data types certifies a real position, machine reference or execution.
namespace JobPlacement {
enum class Mode { Absolute, CurrentPosition, UserOrigin };
enum class Anchor {
    LowerLeft,
    LowerCenter,
    LowerRight,
    CenterLeft,
    Center,
    CenterRight,
    UpperLeft,
    UpperCenter,
    UpperRight
};
enum class OriginScope { Design, Selection };

struct PositionSnapshot {
    QPointF point;
    bool known = false;
    QByteArray profileFingerprint;
    QByteArray
        coordinateFrameFingerprint; // Caller changes this after reference/work-offset changes.
    quint64 revision = 0; // Explicit caller snapshot identity; never inferred from point == 0,0.
};

class SourceSnapshot {
  public:
    static SourceSnapshot capture(const JobPlan &plan, const QRectF &graphicsBounds,
                                  const QByteArray &geometryFingerprint, OriginScope scope,
                                  const Cancellation &cancel = {});
    bool valid() const { return m_error.isEmpty() && !m_fingerprint.isEmpty(); }
    QString error() const { return m_error; }
    QRectF graphicsBounds() const { return m_bounds; }
    OriginScope scope() const { return m_scope; }
    QByteArray fingerprint() const { return m_fingerprint; }
    QByteArray geometryFingerprint() const { return m_geometry; }
    QByteArray profileFingerprint() const { return m_profile; }
    bool matches(const JobPlan &plan, const QByteArray &geometryFingerprint,
                 const Cancellation &cancel = {}) const;

  private:
    QRectF m_bounds;
    OriginScope m_scope = OriginScope::Design;
    QByteArray m_fingerprint, m_geometry, m_profile, m_motion;
    QString m_error;
};

struct Request {
    Mode mode = Mode::Absolute;
    Anchor anchor = Anchor::LowerLeft; // Ignored in Absolute mode, as in LightBurn.
    QByteArray expectedSourceFingerprint;
    std::optional<PositionSnapshot> startPosition;
    std::optional<PositionSnapshot> userOrigin;
};

class Result {
  public:
    bool valid() const { return m_error.isEmpty() && m_plan.valid(); }
    QString error() const { return m_error; }
    const JobPlan &plan() const { return m_plan; } // Preview/data only, never a hardware grant.
    QPointF translation() const { return m_translation; }
    QRectF placedGraphicsBounds() const { return m_graphicsBounds; }
    bool explicitApproach() const { return m_explicitApproach; }
    QByteArray sourceFingerprint() const { return m_source; }
    QByteArray requestFingerprint() const { return m_request; }
    bool matches(const JobPlan &plan, const SourceSnapshot &source, const Request &request,
                 const Cancellation &cancel = {}) const;

  private:
    friend Result place(const JobPlan &, const SourceSnapshot &, const Request &,
                        const Cancellation &);
    JobPlan m_plan;
    QString m_error;
    QPointF m_translation;
    QRectF m_graphicsBounds;
    QByteArray m_source, m_request;
    bool m_explicitApproach = false;
};
Result place(const JobPlan &plan, const SourceSnapshot &source, const Request &request,
             const Cancellation &cancel = {});
} // namespace JobPlacement
