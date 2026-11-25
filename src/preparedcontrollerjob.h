#pragma once
#include "cancellation.h"
#include <QByteArray>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QStringList>
#include <memory>

struct JobPlan;

// Immutable output data, NOT a motion/flow/connection permission. Preparation
// scans/encodes the plan and must run outside the controller runtime thread.
// No Document, Artwork, image, renderer or live QObject crosses this boundary.
class PreparedControllerJob final {
  public:
    using Ptr = std::shared_ptr<const PreparedControllerJob>;
    struct Visualization final {
        using Ptr=std::shared_ptr<const Visualization>;
        QImage image; // Bounded raster, transparent, top row = maximum work Y.
        QSizeF workArea; // Frozen profile bed, independent of the next editor design.
        QRectF workBounds; // Raster extent, including a small non-motion display padding.
        QByteArray sourceFingerprint;
        double estimatedSeconds=0;
        qsizetype moveCount=0;
    };
    Visualization::Ptr visualization() const { return m_visualization; }
    static Ptr prepare(const JobPlan &plan, const Cancellation &cancel = {});
    bool valid() const { return !m_cancelled && m_errors.isEmpty() && !m_lines.isEmpty(); }
    bool cancelled() const { return m_cancelled; }
    const QStringList &errors() const { return m_errors; }
    const QStringList &lines() const { return m_lines; }
    const QByteArray &profileFingerprint() const { return m_profile; }
    const QByteArray &sourceFingerprint() const { return m_source; }
    const QString &driverId() const { return m_driver; }
    const QString &transport() const { return m_transport; }
    const QString &mediaType() const { return m_media; }
    // Raw G54 work endpoints. Runtime must apply its current WCO, command
    // rounding and signed envelope contract, and also check actual raw MPos.
    // Degenerate bounds (horizontal/vertical/single-point) are valid data.
    QRectF workBounds() const { return m_bounds; }
    QPointF firstApproach() const { return m_firstApproach; }
    qsizetype moveCount() const { return m_moveCount; }

  private:
    PreparedControllerJob() = default;
    Visualization::Ptr m_visualization;
    QStringList m_lines, m_errors;
    QByteArray m_profile, m_source;
    QString m_driver, m_transport, m_media;
    QRectF m_bounds;
    QPointF m_firstApproach;
    qsizetype m_moveCount = 0;
    bool m_cancelled = false;
};
