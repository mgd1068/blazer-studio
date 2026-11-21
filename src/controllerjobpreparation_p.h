#pragma once
// Implementation/test seam. No transport, execution or commissioning API.
#include "controllerjobpreparation.h"
#include "job.h"
#include <QMutex>
#include <QWaitCondition>
#include <functional>
#include <optional>

struct ControllerJobPreparationRequest {
    quint64 generation = 0, epoch = 0;
    JobPlan plan;
    Cancellation cancel;
};
struct ControllerJobPreparationState {
    QMutex mutex;
    QWaitCondition wake;
    ControllerJobPreparation *owner = nullptr; // Protected through invocation registration.
    std::optional<ControllerJobPreparationRequest> latest;
    Cancellation activeCancel;
    quint64 newestGeneration = 0, epoch = 0, activeEpoch = 0, completedEpoch = 0;
    quint64 deliveredEpoch = 0;
    PreparedControllerJob::Ptr completed;
    QString error;
    bool closed = false, active = false, executorStarted = false, executorStopped = false;
    bool notificationPending = false, cancelled = false;
    std::function<PreparedControllerJob::Ptr(const JobPlan &, const Cancellation &)> work =
        [](const JobPlan &plan, const Cancellation &cancel) {
            return PreparedControllerJob::prepare(plan, cancel);
        };
};
