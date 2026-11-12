#pragma once
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <algorithm>
#include <cmath>

namespace Geometry {
inline constexpr double bedWidth = 200.;
inline constexpr double bedHeight = 150.;
inline constexpr double coordinateLimit = 2000.;
inline constexpr double minimumSize = .1;
inline constexpr int objectLimit = 2000;
inline bool validBounds(const QRectF &r) {
    return std::isfinite(r.x()) && std::isfinite(r.y()) && std::isfinite(r.width()) &&
           std::isfinite(r.height()) && std::abs(r.x()) <= coordinateLimit &&
           std::abs(r.y()) <= coordinateLimit && r.width() >= minimumSize &&
           r.height() >= minimumSize && r.width() <= coordinateLimit &&
           r.height() <= coordinateLimit;
}
inline bool insideBed(const QRectF &r, QSizeF bed = {bedWidth, bedHeight}) {
    return validBounds(r) && r.left() >= 0 && r.top() >= 0 && r.right() <= bed.width() &&
           r.bottom() <= bed.height();
}
// Both arguments use mm. QRectF's minimum y represents the lower edge in world space.
inline QPointF worldToCanvas(QPointF world) {
    return {world.x(), bedHeight - world.y()};
}
inline QRectF worldToCanvas(QRectF world) {
    return {world.x(), bedHeight - world.y() - world.height(), world.width(), world.height()};
}
inline QPointF framePoint(const QRectF &r, double progress) {
    const double distance = std::clamp(progress, 0., 1.) * 2 * (r.width() + r.height());
    if (distance <= r.width())
        return {r.x() + distance, r.y()};
    if (distance <= r.width() + r.height())
        return {r.right(), r.y() + distance - r.width()};
    if (distance <= 2 * r.width() + r.height())
        return {r.right() - (distance - r.width() - r.height()), r.bottom()};
    return {r.x(), r.bottom() - (distance - 2 * r.width() - r.height())};
}
} // namespace Geometry
