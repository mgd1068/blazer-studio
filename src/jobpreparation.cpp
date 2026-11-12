#include "jobpreparation.h"
#include <QtConcurrentRun>
#include <exception>
JobPreparation::JobPreparation(QObject *parent, Builder builder)
    : QObject(parent), m_builder(std::move(builder)) {
    if (!m_builder)
        m_builder = [](const JobInput &input, const Cancellation &cancel) {
            return JobPlanner::build(input, cancel);
        };
    m_pool.setMaxThreadCount(1);
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_debounce, &QTimer::timeout, this, &JobPreparation::startPending);
    connect(&m_watcher, &QFutureWatcher<Result>::finished, this, &JobPreparation::finish);
    m_plan.errors = {"Noch kein Auftrag berechnet."};
}
JobPreparation::~JobPreparation() {
    m_debounce.stop();
    if (m_cancel)
        m_cancel->store(true, std::memory_order_relaxed);
    disconnect(&m_watcher, nullptr, this, nullptr);
    m_pool.waitForDone();
}
bool JobPreparation::busy() const {
    return m_pending.has_value() || m_active || m_debounce.isActive();
}
void JobPreparation::invalidate(QString state, QString message) {
    ++m_revision;
    if (m_cancel)
        m_cancel->store(true, std::memory_order_relaxed);
    m_plan = {};
    m_plan.errors = {std::move(message)};
    m_state = std::move(state);
}
void JobPreparation::submit(JobInput input) {
    invalidate("queued", "Auftrag wird im Hintergrund berechnet.");
    m_pending = std::move(input);
    m_debounce.start();
    emit changed();
}
void JobPreparation::cancel() {
    invalidate(m_active ? "cancelling" : "cancelled",
               "Berechnung abgebrochen. Bei Bedarf neu berechnen.");
    m_pending.reset();
    m_debounce.stop();
    emit changed();
}
void JobPreparation::startPending() {
    if (!m_pending || m_active)
        return;
    auto input = std::move(*m_pending);
    m_pending.reset();
    auto flag = std::make_shared<std::atomic_bool>(false);
    m_cancel = flag;
    const auto revision = m_revision;
    auto builder = m_builder;
    m_active = true;
    m_state = "running";
    m_watcher.setFuture(QtConcurrent::run(&m_pool, [input = std::move(input), flag, revision,
                                                    builder]() {
        JobPlan plan(input.deviceProfile);
        try {
            plan = builder(input, flag);
            if (plan.deviceProfile().outputFingerprint() !=
                input.deviceProfile.outputFingerprint()) {
                plan = JobPlan(input.deviceProfile);
                plan.errors = {"Auftragsberechnung lieferte ein abweichendes Geräteprofil."};
            }
            if (plan.valid() && !isCancelled(flag))
                plan.cacheCommands(flag);
        } catch (const std::exception &) {
            plan.errors = {"Auftrag konnte nicht berechnet werden (Ressourcen- oder Datenfehler)."};
        } catch (...) {
            plan.errors = {"Unerwarteter Fehler bei der Auftragsberechnung."};
        }
        if (isCancelled(flag)) {
            plan = JobPlan(input.deviceProfile);
            plan.cancelled = true;
        }
        return Result{revision, std::move(plan)};
    }));
    emit changed();
}
void JobPreparation::finish() {
    const auto result = m_watcher.result();
    m_active = false;
    if (result.revision == m_revision && !result.plan.cancelled) {
        m_plan = result.plan;
        m_state = m_plan.valid() ? "ready" : "failed";
    } else if (!m_pending)
        m_state = "cancelled";
    if (m_pending && !m_debounce.isActive())
        m_debounce.start(0);
    emit changed();
}
