#pragma once
#include "preparedcontrollerjob.h"
#include <QObject>
#include <memory>

struct JobPlan;
struct ControllerJobPreparationState;
class ControllerJobPreparationTest;

// Data preparation only. Exactly one active and one latest pending plan;
// no transport, hardware permission or automatic execution on completion.
class ControllerJobPreparation final : public QObject {
    Q_OBJECT
  public:
    explicit ControllerJobPreparation(QObject *parent = nullptr);
    ~ControllerJobPreparation() override;
    bool prepare(const JobPlan &plan, quint64 candidateGeneration);
    void cancel();
    void close(); // Permanent admission revocation; returns without joining worker.
    bool preparing() const;
    QString reason() const;
    quint64 generation() const;
    PreparedControllerJob::Ptr result(quint64 expectedGeneration) const;
  signals:
    void changed();
    // Signal carries an identity, never an execution request. result(id) checks
    // freshness again, including reentrant edits/cancellation in signal slots.
    void finished(quint64 candidateGeneration);
  private:
    friend class ControllerJobPreparationTest;
    friend class ControllerJobPreparationRunner;
    std::shared_ptr<ControllerJobPreparationState> m_state;
    void deliver();
};
