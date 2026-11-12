#include "document.h"
#include "geometry.h"
#include <algorithm>
#include <cmath>

QVariantMap Document::framingBounds(const QString &scope) const {
    auto invalid = [&](const QString &reason) {
        return QVariantMap{
            {"valid", false},
            {"scope", scope},
            {"reason", reason},
            {"outputFingerprint", QString::fromLatin1(m_deviceProfile.outputFingerprint())}};
    };
    if (!QStringList{"job", "selection", "tools", "t1", "t2"}.contains(scope))
        return invalid("Unbekanntes Ziel der Rahmenfahrt.");
    bool first = true;
    double left = 0, right = 0, bottom = 0, top = 0;
    auto include = [&](QPointF p) {
        if (first) {
            left = right = p.x();
            bottom = top = p.y();
            first = false;
        } else {
            left = std::min(left, p.x());
            right = std::max(right, p.x());
            bottom = std::min(bottom, p.y());
            top = std::max(top, p.y());
        }
    };
    if (scope == "job") {
        if (planningBusy())
            return invalid("Auftragsberechnung läuft noch.");
        const auto &plan = job();
        if (!plan.valid())
            return invalid("Kein gültiger Laserauftrag für eine Rahmenfahrt.");
        // Include non-rapid travel such as overscan, but not travel from origin
        // to the first shape or between separate objects.
        for (const auto &move : plan.moves)
            if (!move.rapid) {
                include(move.from);
                include(move.to);
            }
    } else {
        for (int i = 0; i < m_items.size(); ++i) {
            const auto &a = m_items[i];
            if (!a.visible || a.page != m_activePage)
                continue;
            const bool selected = scope == "selection" && m_selection.contains(i);
            const bool helper = (scope == "tools" && a.toolLayer != 0) ||
                                (scope == "t1" && a.toolLayer == 1) ||
                                (scope == "t2" && a.toolLayer == 2);
            if (selected || helper) {
                include(a.bounds.topLeft());
                include(a.bounds.bottomRight());
            }
        }
    }
    if (first)
        return invalid(scope == "job" ? "Keine geplanten Bearbeitungswege vorhanden."
                       : scope == "selection"
                           ? "Keine sichtbare Auswahl auf diesem Blatt."
                           : "Keine passenden sichtbaren Hilfsformen auf diesem Blatt.");
    // Match planner round-off tolerance for its paths. Selection/helper boxes
    // are explicit document coordinates and must be inside the logical bed.
    const double tolerance = scope == "job" ? .00001 : 0;
    if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(bottom) ||
        !std::isfinite(top) || left < -tolerance || bottom < -tolerance ||
        right > bedWidth() + tolerance || top > bedHeight() + tolerance)
        return invalid("Ziel der Rahmenfahrt liegt außerhalb des Arbeitsraums.");
    left = std::max(0., left);
    right = std::min(bedWidth(), right);
    bottom = std::max(0., bottom);
    top = std::min(bedHeight(), top);
    // A straight path still gets a valid, narrow rectangular frame. Keep its
    // entire span inside the bed, including a line exactly on a bed boundary.
    const double width = std::max(Geometry::minimumSize, right - left);
    const double height = std::max(Geometry::minimumSize, top - bottom);
    const double x = std::clamp(left - (width - (right - left)) / 2, 0., bedWidth() - width);
    const double y = std::clamp(bottom - (height - (top - bottom)) / 2, 0., bedHeight() - height);
    const QRectF rect(x, y, width, height);
    if (!Geometry::insideBed(rect, workArea()))
        return invalid("Ungültige Grenzen der Rahmenfahrt.");
    return {{"outputFingerprint", QString::fromLatin1(m_deviceProfile.outputFingerprint())},
            {"valid", true},
            {"scope", scope},
            {"reason", QString()},
            {"x", x},
            {"y", y},
            {"width", width},
            {"height", height}};
}
