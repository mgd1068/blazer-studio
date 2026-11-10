#pragma once
#include "job.h"
#include <QFutureWatcher>
#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <functional>
#include <optional>

// Only value snapshots enter the worker. One running job plus one latest pending
// snapshot bounds CPU and memory; obsolete generations are never published.
class JobPreparation : public QObject {
    Q_OBJECT
  public:
    using Builder = std::function<JobPlan(const JobInput &, const Cancellation &)>;
    explicit JobPreparation(QObject *parent = nullptr, Builder builder = {});
    ~JobPreparation() override;
    void submit(JobInput input);
    void cancel();
    bool busy() const;
    QString state() const { return m_state; }
    const JobPlan &plan() const { return m_plan; }
  signals:
    void changed();

  private:
    void startPending();
    void finish();
    void invalidate(QString state, QString message);
    struct Result {
        quint64 revision;
        JobPlan plan;
    };
    QThreadPool m_pool;
    QFutureWatcher<Result> m_watcher;
    QTimer m_debounce;
    Builder m_builder;
    Cancellation m_cancel;
    std::optional<JobInput> m_pending;
    bool m_active = false;
    quint64 m_revision = 0;
    JobPlan m_plan;
    QString m_state = "idle";
};
