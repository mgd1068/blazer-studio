#pragma once
#include "job.h"
namespace VectorInspection {
struct Finding {
    QString code, severity, label;
    QStringList objectIds;
    int contourIndex = -1;
    QPointF positionMm;
};
struct Report {
    QList<Finding> findings;
    bool complete = true, cancelled = false;
    int inspectedObjects = 0, segmentCount = 0, pairChecks = 0;
    QVariantMap toVariantMap() const;
};
// Explicit snapshot operation, never automatic per-frame inspection. Empty
// selection means all active visible output. Work on a captured JobInput only.
Report inspect(const JobInput &snapshot, const QStringList &selectedIds = {},
               const Cancellation &cancel = {});
} // namespace VectorInspection
