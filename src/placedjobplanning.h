#pragma once
#include "jobplacement.h"
#include <QJsonValue>

namespace PlacedJobPlanning {
struct Intent {
    JobPlacement::Mode mode = JobPlacement::Mode::Absolute;
    JobPlacement::Anchor anchor = JobPlacement::Anchor::LowerLeft;
    std::optional<QPointF> userOrigin;
    bool cutSelectedGraphics = false;
    bool useSelectionOrigin = false;
    bool operator==(const Intent &other) const {
        if (mode != other.mode || anchor != other.anchor ||
            cutSelectedGraphics != other.cutSelectedGraphics ||
            useSelectionOrigin != other.useSelectionOrigin ||
            userOrigin.has_value() != other.userOrigin.has_value())
            return false;
        return !userOrigin || (userOrigin->x() == other.userOrigin->x() &&
                               userOrigin->y() == other.userOrigin->y());
    }
};
struct Context {
    Intent intent;
    std::optional<JobPlacement::PositionSnapshot> startPosition;
    std::optional<JobPlacement::PositionSnapshot> confirmedOrigin;
};
struct Input {
    JobInput document;
    Context placement;
};
struct Result {
    JobPlan plan;
    QVariantMap summary;
};
bool validPosition(const JobPlacement::PositionSnapshot &position, const DeviceProfile &profile);
bool defaultIntent(const Intent &intent);
QJsonObject serializeIntent(const Intent &intent);
std::optional<Intent> parseIntent(const QJsonValue &json, int schemaVersion = 1);
Result finish(const Input &input, JobPlan canonical, const Cancellation &cancel = {});
Result build(const Input &input, const Cancellation &cancel = {});
} // namespace PlacedJobPlanning
