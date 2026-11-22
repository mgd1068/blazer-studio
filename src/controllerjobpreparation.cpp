#include "controllerjobpreparation_p.h"
#include <QCoreApplication>
#include <QMutexLocker>
#include <QPointer>
#include <QSignalBlocker>
#include <QThread>

// Compute-only thread: run() owns plain immutable plan snapshots, never serial
// devices, timers, GUI objects or QObject slots. It outlives a cancelled facade
// until its current bounded input has unwound; the facade never joins it.
class ControllerJobPreparationRunner final : public QThread {
  public:
    explicit ControllerJobPreparationRunner(std::shared_ptr<ControllerJobPreparationState> state)
        : m_state(std::move(state)) {}
  private:
    std::shared_ptr<ControllerJobPreparationState> m_state;
    void run() override {
        for (;;) {
            std::optional<ControllerJobPreparationRequest> request;
            decltype(m_state->work) work;
            {
                QMutexLocker lock(&m_state->mutex);
                while (!m_state->closed && !m_state->latest) m_state->wake.wait(&m_state->mutex);
                if (m_state->closed) break;
                request.swap(m_state->latest);
                m_state->active = true;
                m_state->activeEpoch = request->epoch;
                m_state->activeCancel = request->cancel;
                work = m_state->work;
            }
            PreparedControllerJob::Ptr result, obsolete;
            PreparedLocalJob::Ptr localResult,obsoleteLocal;
            QString error;
            try {
                if(request->local) {
                    auto input=*request->local;
                    if(input.expectedSourceFingerprint.isEmpty())input.expectedSourceFingerprint=ManualTravelCheck::sourceFingerprint(request->plan,request->cancel);
                    localResult=PreparedLocalJob::prepare(request->plan,input,request->cancel);
                    result=localResult?localResult->prepared():PreparedControllerJob::Ptr{};
                    if(localResult && !localResult->valid())error=localResult->errors().join("; ");
                } else result = work(request->plan, request->cancel);
                if ((!result || !result->valid()) && error.isEmpty())
                    error = result ? result->errors().join("; ") : QString("Controllerausgabe konnte nicht vorbereitet werden.");
            } catch (...) {
                error = "Controllerausgabe konnte nicht vorbereitet werden.";
            }
            {
                QMutexLocker lock(&m_state->mutex);
                m_state->active = false;
                m_state->activeCancel.reset();
                if (!m_state->closed && request->epoch == m_state->epoch &&
                    !isCancelled(request->cancel)) {
                    obsolete.swap(m_state->completed);
                    obsoleteLocal.swap(m_state->completedLocal);
                    m_state->completedLocal=std::move(localResult);
                    m_state->completed = std::move(result);
                    m_state->completedEpoch = request->epoch;
                    m_state->error = std::move(error);
                    if (m_state->owner && !m_state->notificationPending) {
                        m_state->notificationPending = true;
                        auto owner = m_state->owner;
                        QMetaObject::invokeMethod(owner, [owner] { owner->deliver(); }, Qt::QueuedConnection);
                    }
                }
            }
            // Large discarded plans/results are released outside the mutex and
            // outside the controller runtime, including cancelled completions.
        }
        QMutexLocker lock(&m_state->mutex);
        m_state->executorStopped = true;
    }
};

ControllerJobPreparation::ControllerJobPreparation(QObject *parent)
    : QObject(parent), m_state(std::make_shared<ControllerJobPreparationState>()) {
    m_state->owner = this;
    if (auto app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this, &ControllerJobPreparation::close);
}
ControllerJobPreparation::~ControllerJobPreparation() {
    const QSignalBlocker notifications(this);
    close();
}

bool ControllerJobPreparation::prepare(const JobPlan &plan,quint64 generation){return prepareImpl(plan,generation,{});}
bool ControllerJobPreparation::prepareLocal(const JobPlan &plan,const ManualTravelCheck::Request &request,quint64 generation){return prepareImpl(plan,generation,request);}
bool ControllerJobPreparation::prepareImpl(const JobPlan &plan, quint64 generation,std::optional<ManualTravelCheck::Request> local) {
    if (thread() != QThread::currentThread() || !generation) return false;
    auto snapshot = plan;
    snapshot.previews.clear(); // Encoding does not need raster preview ownership.
    std::optional<ControllerJobPreparationRequest> superseded;
    PreparedControllerJob::Ptr obsolete;
    PreparedLocalJob::Ptr obsoleteLocal;
    bool startExecutor = false;
    {
        QMutexLocker lock(&m_state->mutex);
        if (m_state->closed || generation <= m_state->newestGeneration) return false;
        if (m_state->activeCancel) m_state->activeCancel->store(true);
        if (m_state->latest) m_state->latest->cancel->store(true);
        superseded.swap(m_state->latest);
        obsolete.swap(m_state->completed);
        obsoleteLocal.swap(m_state->completedLocal);
        m_state->completedEpoch = 0;
        m_state->error.clear(); m_state->cancelled = false;
        m_state->newestGeneration = generation;
        m_state->latest.emplace(ControllerJobPreparationRequest{
            generation, ++m_state->epoch, std::move(snapshot), std::make_shared<std::atomic_bool>(false),std::move(local)});
        startExecutor = !m_state->executorStarted;
        m_state->executorStarted = true;
        m_state->wake.wakeOne();
    }
    if (startExecutor) {
        auto executor = new ControllerJobPreparationRunner(m_state);
        // Parentless intentionally: destroying the GUI facade must not destroy
        // a running QThread. Finished queues deletion on the creator's thread.
        connect(executor, &QThread::finished, executor, &QObject::deleteLater);
        executor->start();
    }
    emit changed();
    return true;
}
void ControllerJobPreparation::cancel() {
    if (thread() != QThread::currentThread()) return;
    std::optional<ControllerJobPreparationRequest> superseded;
    PreparedControllerJob::Ptr obsolete;
    PreparedLocalJob::Ptr obsoleteLocal;
    {
        QMutexLocker lock(&m_state->mutex);
        if (m_state->closed) return;
        ++m_state->epoch;
        m_state->cancelled = true;
        if (m_state->activeCancel) m_state->activeCancel->store(true);
        if (m_state->latest) m_state->latest->cancel->store(true);
        superseded.swap(m_state->latest);
        obsolete.swap(m_state->completed);
        obsoleteLocal.swap(m_state->completedLocal);
        m_state->completedEpoch = 0;
        m_state->error.clear();
    }
    emit changed();
}
void ControllerJobPreparation::close() {
    std::optional<ControllerJobPreparationRequest> superseded;
    PreparedControllerJob::Ptr obsolete;
    PreparedLocalJob::Ptr obsoleteLocal;
    {
        QMutexLocker lock(&m_state->mutex);
        if (m_state->closed) return;
        m_state->closed = true; m_state->owner = nullptr;
        ++m_state->epoch;
        if (m_state->activeCancel) m_state->activeCancel->store(true);
        if (m_state->latest) m_state->latest->cancel->store(true);
        superseded.swap(m_state->latest);
        obsolete.swap(m_state->completed);
        obsoleteLocal.swap(m_state->completedLocal);
        m_state->completedEpoch = 0;
        m_state->wake.wakeOne();
    }
    emit changed();
}
bool ControllerJobPreparation::preparing() const {
    QMutexLocker lock(&m_state->mutex);
    return !m_state->closed && (m_state->latest ||
        (m_state->active && m_state->activeEpoch == m_state->epoch));
}
QString ControllerJobPreparation::reason() const {
    QMutexLocker lock(&m_state->mutex);
    if (m_state->closed) return "Controllervorbereitung beendet.";
    if (m_state->cancelled) return "Controllervorbereitung abgebrochen.";
    if (m_state->latest || (m_state->active && m_state->activeEpoch == m_state->epoch))
        return "Controllerausgabe wird vorbereitet; noch keine Startfreigabe.";
    if (!m_state->error.isEmpty()) return m_state->error;
    return m_state->completed && m_state->completed->valid()
        ? QString() : QString("Controllerausgabe noch nicht vorbereitet.");
}
quint64 ControllerJobPreparation::generation() const {
    QMutexLocker lock(&m_state->mutex); return m_state->newestGeneration;
}
PreparedControllerJob::Ptr ControllerJobPreparation::result(quint64 expected) const {
    QMutexLocker lock(&m_state->mutex);
    if (m_state->closed || m_state->cancelled || expected != m_state->newestGeneration ||
        m_state->completedEpoch != m_state->epoch || m_state->latest ||
        (m_state->active && m_state->activeEpoch == m_state->epoch)) return {};
    return m_state->completed;
}
PreparedLocalJob::Ptr ControllerJobPreparation::localResult(quint64 expected) const {
    QMutexLocker lock(&m_state->mutex);
    if(m_state->closed||m_state->cancelled||expected!=m_state->newestGeneration||m_state->completedEpoch!=m_state->epoch||m_state->latest||(m_state->active&&m_state->activeEpoch==m_state->epoch))return {};
    return m_state->completedLocal;
}
void ControllerJobPreparation::deliver() {
    {
        QMutexLocker lock(&m_state->mutex);
        m_state->notificationPending = false;
        if (m_state->closed || m_state->owner != this) return;
    }
    const QPointer<ControllerJobPreparation> alive(this);
    emit changed();
    if (!alive) return; // Direct receivers may legally destroy the sender.
    quint64 finishedGeneration = 0;
    {
        QMutexLocker lock(&m_state->mutex);
        if (m_state->closed || m_state->completedEpoch != m_state->epoch ||
            m_state->completedEpoch == m_state->deliveredEpoch || m_state->latest) return;
        m_state->deliveredEpoch = m_state->completedEpoch;
        finishedGeneration = m_state->newestGeneration;
    }
    emit finished(finishedGeneration);
}
