#pragma once
#include "manualtravelcheck.h"
#include "preparedcontrollerjob.h"

// Immutable data, never a machine pose, local-frame lease or output permission.
// Input is ALREADY placed in virtual G54 (including the chosen current-position
// anchor); first.from must equal the explicitly confirmed G54 head. No rebase,
// profile alteration, clipping or repair occurs here. Prepare outside runtime.
class PreparedLocalJob final {
  public:
    using Ptr = std::shared_ptr<const PreparedLocalJob>;
    static constexpr double safetyMarginMm = 2.;
    static Ptr prepare(const JobPlan &, const ManualTravelCheck::Request &,
                       const Cancellation &cancel = {});
    bool valid() const { return m_prepared && m_errors.isEmpty() && !m_cancelled; }
    bool cancelled() const { return m_cancelled; }
    const QStringList &errors() const { return m_errors; }
    PreparedControllerJob::Ptr prepared() const { return m_prepared; }
    // Job motion and output-profile identity (same as PreparedControllerJob).
    const QByteArray &sourceFingerprint() const { return m_source; }
    const QByteArray &profileFingerprint() const { return m_profile; }
    // Full manual source identity includes both motion and profile.
    const QByteArray &manualTravelSourceFingerprint() const { return m_manualSource; }
    const QByteArray &requestFingerprint() const { return m_request; }
    // Measurement-frame data identity, NOT a connection/session authority.
    const QByteArray &localFrameFingerprint() const { return m_frame; }
    const QByteArray &fingerprint() const { return m_fingerprint; }
    QPointF headAnchor() const { return m_head; }
    ManualTravelCheck::Distances available() const { return m_available; }
    ManualTravelCheck::Distances usableAfterMargin() const { return m_usable; }
    ManualTravelCheck::Distances requiredRaw() const { return m_raw; }
    ManualTravelCheck::Distances requiredActualWire() const { return m_wire; }
    // Conservative union also retains the existing pure local-quantization check.
    ManualTravelCheck::Distances requiredUnion() const { return m_union; }
    QRectF rawLocalBounds() const { return m_rawLocalBounds; }
    QRectF actualAbsoluteWireBounds() const { return m_absoluteWireBounds; }
    QRectF actualLocalWireBounds() const { return m_localWireBounds; }
    QPointF rawFirstApproach() const { return m_rawFirst; }
    QPointF firstApproach() const { return m_wireFirst; }
    // Zero-based index in prepared()->lines(); -1 if all targets equal the head.
    qsizetype firstNonzeroMotionLineIndex() const { return m_firstNonzero; }
    qsizetype motionLineCount() const { return m_motionLines; }
    bool matches(const JobPlan &, const ManualTravelCheck::Request &,
                 const Cancellation &cancel = {}) const;

  private:
    friend class PreparedLocalJobTest; // Malformed-wire tests only, no runtime grant.
    PreparedLocalJob() = default;
    bool inspectWire(const QStringList &, double powerScale, const QSizeF &bed,
                     const Cancellation &);
    PreparedControllerJob::Ptr m_prepared;
    ManualTravelCheck::Result m_pure;
    QStringList m_errors;
    QByteArray m_source, m_profile, m_manualSource, m_request, m_frame, m_fingerprint;
    QPointF m_head, m_rawFirst, m_wireFirst;
    ManualTravelCheck::Distances m_available, m_usable, m_raw, m_wire, m_union;
    QRectF m_rawLocalBounds, m_absoluteWireBounds, m_localWireBounds;
    qsizetype m_firstNonzero = -1, m_motionLines = 0;
    bool m_cancelled = false;
};
