#include "controllerruntime.h"
#include "controllerruntimemailbox.h"
#include "controllerruntimeworker.h"
#include "preparedcontrollerjob.h"
#include "job.h"
#include "support/runtimepeer.h"
#include <QJsonArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QMutexLocker>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

using RuntimeTestSupport::RuntimePeer;

// Named private production friend. All authority/evidence setup and inspection
// runs in the worker. No public/QML grant or synchronous runtime mode is added.
class ControllerRuntimeTestAccess {
  public:
    struct Probe {
        bool initialized = false, affinity = false, lease = false, releaseRequired = false;
        bool job = false, focus = false, awaitingAck = false;
        quint64 generation = 0, epoch = 0;
        qint64 flowAtMs = -1, clockMs = -1, statusAtNs = -1, ackDueAtNs = -1;
        QString extractionError;
    };
    struct BarrierState {
        QSemaphore entered, release;
        std::atomic_bool timedOut{false};
    };
    struct Barrier {
        std::shared_ptr<BarrierState> state;
        explicit Barrier(std::shared_ptr<BarrierState> value) : state(std::move(value)) {}
        ~Barrier() { release(); }
        void release() { state->release.release(); }
        bool timedOut() const { return state->timedOut.load(); }
    };
    template <typename Function>
    static bool run(ControllerRuntimeSession &runtime, Function function, int timeoutMs = 3000) {
        struct Shared { QSemaphore done; std::atomic_bool cancelled{false}; bool ok = false; };
        auto shared = std::make_shared<Shared>();
        auto worker = runtime.m_worker; // Facade-owned pointer only, never read core fields here.
        if (!worker || !QMetaObject::invokeMethod(worker, [worker, shared, function] {
                if (!shared->cancelled.load() && worker->m_core) {
                    try { shared->ok = function(*worker); } catch (...) { shared->ok = false; }
                }
                shared->done.release();
            }, Qt::QueuedConnection))
            return false;
        if (!shared->done.tryAcquire(1, timeoutMs)) {
            shared->cancelled.store(true);
            return false;
        }
        return shared->ok;
    }
    static std::optional<Probe> probe(ControllerRuntimeSession &runtime) {
        auto result = std::make_shared<Probe>();
        const auto gui = QThread::currentThread();
        const bool ok = run(runtime, [result, gui](ControllerRuntimeWorker &worker) {
            const auto thread = QThread::currentThread();
            auto &core = *worker.m_core;
            result->initialized = true;
            result->affinity = thread != gui && worker.thread() == thread && core.thread() == thread &&
                core.serial.thread() == thread && core.poll.thread() == thread &&
                core.timeout.thread() == thread && core.ackTimeout.thread() == thread && core.focusTimer.thread() == thread &&
                core.workZeroDeadline.thread() == thread && worker.m_watchdog &&
                worker.m_watchdog->thread() == thread;
            result->lease = worker.m_accessory.activeLease();
            result->releaseRequired = worker.m_accessory.releaseRequired();
            result->job = worker.m_jobActive;
            result->awaitingAck = core.waitingAck;
            if (!core.ackDue.isEmpty())
                result->ackDueAtNs = RuntimePeer::monotonicNs() +
                    (core.ackDue.first() - core.ackClock.elapsed()) * 1000000LL;
            result->focus = worker.m_focusActive;
            result->generation = worker.m_mailbox->generation.load();
            result->epoch = worker.m_connectionEpoch;
            result->flowAtMs = worker.m_accessory.state(AccessoryContract::Role::EnclosureExtraction).observedAtMs;
            result->clockMs = worker.m_accessoryClock.elapsed();
            result->extractionError = worker.m_accessory.preflightError(result->clockMs);
            result->statusAtNs = core.lastStatus.isValid()
                ? RuntimePeer::monotonicNs() - core.lastStatus.elapsed() * 1000000LL : -1;
            return true;
        });
        return ok ? std::optional<Probe>(*result) : std::nullopt;
    }
    static bool commission(ControllerRuntimeSession &runtime, std::shared_ptr<std::atomic<qint64>> flowAtNs = {}) {
        return run(runtime, [flowAtNs](ControllerRuntimeWorker &worker) {
            auto &core = *worker.m_core;
            if (!core.ready()) return false;
            core.commissionedEnvelope = QRectF(0, 0, 200, 150);
            core.commissionedModalContract = true;
            core.positionReferenced = true;
            worker.m_accessory = AccessoryContract(true);
            constexpr auto role = AccessoryContract::Role::EnclosureExtraction;
            if (!worker.m_accessory.configure(role, {"synthetic-process-extraction", "private offline fixture only", true}))
                return false;
            const auto request = worker.m_accessory.request(role, true);
            if (!request || !worker.m_accessory.acknowledge(*request, true)) return false;
            const auto now = worker.m_accessoryClock.elapsed();
            if (!worker.m_accessory.observe(role, AccessoryContract::PhysicalState::Flowing,
                    "independent private test observation", now, now)) return false;
            if (flowAtNs) flowAtNs->store(RuntimePeer::monotonicNs());
            worker.publish();
            return core.motionSafetyError().isEmpty() && worker.m_accessory.preflightError(now).isEmpty();
        });
    }
    static bool scheduleFlow(ControllerRuntimeSession &runtime, int delayMs,
                             AccessoryContract::PhysicalState physical,
                             std::shared_ptr<std::atomic<qint64>> observedAtNs = {}) {
        return run(runtime, [delayMs, physical, observedAtNs](ControllerRuntimeWorker &worker) {
            auto target = &worker;
            QTimer::singleShot(delayMs, target, [target, physical, observedAtNs] {
                const auto now = target->m_accessoryClock.elapsed();
                const bool accepted = target->m_accessory.observe(AccessoryContract::Role::EnclosureExtraction,
                    physical, "scheduled worker-context observation", now, now);
                if (observedAtNs && accepted) observedAtNs->store(RuntimePeer::monotonicNs());
                target->publish();
            });
            return true;
        });
    }
    static bool refreshFlow(ControllerRuntimeSession &runtime) {
        return run(runtime, [](ControllerRuntimeWorker &worker) {
            auto timer = new QTimer(&worker);
            timer->setInterval(200);
            auto target = &worker;
            QObject::connect(timer, &QTimer::timeout, target, [target] {
                const auto now = target->m_accessoryClock.elapsed();
                target->m_accessory.observe(AccessoryContract::Role::EnclosureExtraction,
                    AccessoryContract::PhysicalState::Flowing, "worker-context periodic observation", now, now);
            });
            timer->start();
            return true;
        });
    }
    static std::shared_ptr<Barrier> hold(ControllerRuntimeSession &runtime) {
        auto state = std::make_shared<BarrierState>();
        auto guard = std::make_shared<Barrier>(state);
        auto worker = runtime.m_worker;
        if (!worker || !QMetaObject::invokeMethod(worker, [state] {
                state->entered.release();
                if (!state->release.tryAcquire(1, 3000)) state->timedOut.store(true);
            }, Qt::QueuedConnection) || !state->entered.tryAcquire(1, 3000))
            return {};
        return guard;
    }
    static bool injectStampedJog(ControllerRuntimeSession &runtime, quint64 generation, quint64 connectionEpoch) {
        return run(runtime, [generation, connectionEpoch](ControllerRuntimeWorker &worker) {
            ControllerRuntimeRequest request{ControllerRuntimeRequestKind::Jog};
            request.generation = generation;
            request.connectionEpoch = connectionEpoch;
            request.issuedAt = RuntimePeer::monotonicNs() / 1000000;
            request.expiresAt = request.issuedAt + 1000;
            request.x = .75;
            QMutexLocker lock(&worker.m_mailbox->mutex);
            // Deliberately simulate an already stamped old request. enqueue()
            // would legitimately replace the supplied generation with current.
            worker.m_mailbox->normal.push_back(std::move(request));
            worker.m_mailbox->wakeLocked();
            return true;
        });
    }
    static std::shared_ptr<std::atomic_bool> observeWorkerDestruction(ControllerRuntimeSession &runtime) {
        auto destroyed = std::make_shared<std::atomic_bool>(false);
        if (!run(runtime, [destroyed](ControllerRuntimeWorker &worker) {
                QObject::connect(&worker, &QObject::destroyed, [destroyed] { destroyed->store(true); });
                return true;
            })) return {};
        return destroyed;
    }
};

// Test-only ownership fence: the facade intentionally tears down asynchronously.
// Keep the independent peer alive until its actual worker/serial have stopped.
class RuntimeOwner {
  public:
    std::unique_ptr<ControllerRuntimeSession> runtime =
        std::make_unique<ControllerRuntimeSession>(DeviceProfile::m3Reference());
    std::shared_ptr<std::atomic_bool> destroyed =
        ControllerRuntimeTestAccess::observeWorkerDestruction(*runtime);
    ~RuntimeOwner() {
        runtime.reset();
        QElapsedTimer wait;
        wait.start();
        while (destroyed && !destroyed->load() && wait.elapsed() < 2500) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(1);
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (!destroyed || !destroyed->load())
            QTest::qFail("Runtime worker did not finish bounded test teardown before peer EOF", __FILE__, __LINE__);
    }
};

class ControllerRuntimeAdversarialTest : public QObject {
    Q_OBJECT
    const QString script = QStringLiteral(BLAZER_TEST_SOURCE_DIR "/tests/support/runtime_pty_peer.py");
    static PreparedControllerJob::Ptr edge() {
        JobPlan plan;
        plan.moves = {{{20, 30}, {70, 30}, 5000, 100, 0, false}};
        return PreparedControllerJob::prepare(plan); // Outside the controller runtime thread.
    }
    static PreparedControllerJob::Ptr streamingJob() {
        JobPlan plan;
        QPointF point(20, 30);
        for (int i = 0; i < 20000; ++i) {
            const QPointF next(i % 2 ? 20 : 70, 30);
            plan.moves.append({point, next, 5000, 100, 0, false});
            point = next;
        }
        return PreparedControllerJob::prepare(plan);
    }
    static int totalCommands(RuntimePeer &peer) {
        const auto result = peer.snapshot();
        return result.ok ? result.object.value("counts").toObject().value("command").toInt() : -1;
    }
    static QJsonArray events(RuntimePeer &peer) {
        const auto reply = peer.snapshot();
        return reply.ok ? reply.object.value("events").toArray() : QJsonArray{};
    }
    static int countCommand(const QJsonArray &journal, const QString &command) {
        int count = 0;
        for (const auto &value : journal) {
            const auto event = value.toObject();
            if (event.value("kind") == "command" && event.value("value") == command) ++count;
        }
        return count;
    }
    static qint64 firstResetAfter(const QJsonArray &journal, qint64 after) {
        for (const auto &value : journal) {
            const auto event = value.toObject();
            if (event.value("kind") == "realtime" && event.value("value").toInt() == 0x18 &&
                event.value("atNs").toInteger() >= after) return event.value("atNs").toInteger();
        }
        return -1;
    }
    static bool noUnsafeCommandAfter(const QJsonArray &journal, qint64 after) {
        const QRegularExpression xy("^G[01](?: |X|Y)");
        for (const auto &value : journal) {
            const auto event = value.toObject();
            if (event.value("kind") != "command" || event.value("atNs").toInteger() < after) continue;
            const auto command = event.value("value").toString();
            if (xy.match(command).hasMatch() || command.startsWith("M3") || command.startsWith("M4") ||
                command.startsWith("$J=") || command == "$H" || command.startsWith("G10")) return false;
        }
        return true;
    }
    static bool noResumeOrAccessoryOff(const QJsonArray &journal) {
        for (const auto &value : journal) {
            const auto event = value.toObject();
            if (event.value("kind") == "realtime" && event.value("value").toInt() == '~') return false;
            if (event.value("kind") == "command" &&
                QStringList{"M7", "M8", "M9"}.contains(event.value("value").toString())) return false;
        }
        return true;
    }
    bool startPeer(RuntimePeer &peer) {
        return peer.start(script).ok && peer.control({{"action", "set"},
            {"values", QJsonObject{{"runOnPoweredCommand", true}}}}).ok;
    }
    bool open(ControllerRuntimeSession &runtime, RuntimePeer &peer, char binding = 'a') {
        return runtime.requestOpen(peer.port(), QByteArray(64, binding)).enqueued;
    }
  private slots:
    void init() {
        QTest::failOnWarning(QRegularExpression(".*(?:different thread|Timers cannot|Socket notifiers|No such method).*"));
    }
    void workerOwnsSerialAndAllSafetyTimers() {
        RuntimePeer peer;
        QVERIFY(startPeer(peer));
        RuntimeOwner owner;
        auto &runtime = *owner.runtime;
        QVERIFY(open(runtime, peer));
        QTRY_VERIFY_WITH_TIMEOUT(runtime.status().ready, 5000);
        const auto probe = ControllerRuntimeTestAccess::probe(runtime);
        QVERIFY(probe && probe->initialized && probe->affinity);
        QVERIFY(!probe->lease);
        QVERIFY(!probe->extractionError.isEmpty());
    }
    void realPreparedAdmissionRequiresPrivateFreshContracts() {
        RuntimePeer peer;
        QVERIFY(startPeer(peer));
        RuntimeOwner owner;
        auto &runtime = *owner.runtime;
        QVERIFY(open(runtime, peer));
        QTRY_VERIFY_WITH_TIMEOUT(runtime.status().ready, 5000);
        const auto candidate = edge();
        QVERIFY(candidate->valid());
        runtime.requestStart(candidate); // Admission may reject immediately or in worker.
        QTest::qWait(100);
        QCOMPARE(countCommand(events(peer), "M4 S1000"), 0);
        QVERIFY(ControllerRuntimeTestAccess::commission(runtime));
        const auto submitted = runtime.requestStart(candidate);
        QVERIFY2(submitted.enqueued, qPrintable(submitted.reason));
        QTRY_COMPARE_WITH_TIMEOUT(countCommand(events(peer), "M4 S1000"), 1, 3000);
        const auto probe = ControllerRuntimeTestAccess::probe(runtime);
        QVERIFY(probe && probe->job && probe->lease);
    }
    void healthyStatusAcksAndFlowContinueDuringGuiBlock_data() {
        QTest::addColumn<int>("blockMs");
        QTest::newRow("400ms") << 400;
        QTest::newRow("2000ms") << 2000;
    }
    void healthyStatusAcksAndFlowContinueDuringGuiBlock() {
        QFETCH(int, blockMs);
        RuntimePeer peer;
        QVERIFY(startPeer(peer));
        RuntimeOwner owner;
        auto &runtime = *owner.runtime;
        QVERIFY(open(runtime, peer));
        QTRY_VERIFY_WITH_TIMEOUT(runtime.status().ready, 5000);
        QVERIFY(ControllerRuntimeTestAccess::commission(runtime));
        QVERIFY(ControllerRuntimeTestAccess::refreshFlow(runtime));
        const auto candidate = streamingJob();
        QVERIFY(candidate->valid());
        QVERIFY(runtime.requestStart(candidate).enqueued);
        QTRY_VERIFY_WITH_TIMEOUT(totalCommands(peer) >= 20, 3000);
        const auto before = peer.snapshot();
        QVERIFY(before.ok);
        const int commandsBefore = before.object.value("counts").toObject().value("command").toInt();
        const int pollsBefore = before.object.value("counts").toObject().value("query").toInt();
        const auto begin = RuntimePeer::monotonicNs();
        std::this_thread::sleep_for(std::chrono::milliseconds(blockMs));
        const auto end = RuntimePeer::monotonicNs();
        const auto after = peer.snapshot();
        QVERIFY(after.ok);
        QVERIFY(after.object.value("counts").toObject().value("command").toInt() > commandsBefore + 10);
        QVERIFY(after.object.value("counts").toObject().value("query").toInt() > pollsBefore);
        QVERIFY(firstResetAfter(after.object.value("events").toArray(), begin) < 0);
        const auto probe = ControllerRuntimeTestAccess::probe(runtime);
        QVERIFY(probe && probe->lease && probe->job && probe->affinity);
        QVERIFY(probe->extractionError.isEmpty());
        QVERIFY(runtime.status().statusFresh);
        qInfo() << "Healthy worker streaming/polling survived GUI block ms" << (end - begin) / 1000000;
    }
    void notFlowingAndUnknownStopBeforeGuiReturns_data() {
        QTest::addColumn<int>("blockMs");
        QTest::addColumn<bool>("unknown");
        for (int duration : {400, 2000})
            for (bool unknown : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(duration).arg(unknown ? "unknown" : "not-flowing")))
                    << duration << unknown;
    }
    void notFlowingAndUnknownStopBeforeGuiReturns() {
        QFETCH(int, blockMs);
        QFETCH(bool, unknown);
        RuntimePeer peer;
        QVERIFY(startPeer(peer));
        RuntimeOwner owner;
        auto &runtime = *owner.runtime;
        QVERIFY(open(runtime, peer));
        QTRY_VERIFY_WITH_TIMEOUT(runtime.status().ready, 5000);
        QVERIFY(ControllerRuntimeTestAccess::commission(runtime));
        QVERIFY(runtime.requestStart(edge()).enqueued);
        QTRY_COMPARE_WITH_TIMEOUT(countCommand(events(peer), "M4 S1000"), 1, 3000);
        auto faultAt = std::make_shared<std::atomic<qint64>>(-1);
        QVERIFY(ControllerRuntimeTestAccess::scheduleFlow(runtime, 75,
            unknown ? AccessoryContract::PhysicalState::Unknown : AccessoryContract::PhysicalState::NotFlowing, faultAt));
        QVERIFY(ControllerRuntimeTestAccess::scheduleFlow(runtime, 200, AccessoryContract::PhysicalState::Flowing));
        bool guiDelivered = false;
        QObject timerContext;
        QTimer::singleShot(20, &timerContext, [&] { guiDelivered = true; });
        const auto begin = RuntimePeer::monotonicNs();
        std::this_thread::sleep_for(std::chrono::milliseconds(blockMs));
        const auto end = RuntimePeer::monotonicNs();
        QVERIFY(!guiDelivered);
        const auto journal = events(peer);
        const auto reset = firstResetAfter(journal, begin);
        QVERIFY(faultAt->load() > begin && faultAt->load() < end);
        QVERIFY(reset >= faultAt->load() && reset < end);
        QVERIFY2(reset - faultAt->load() <= 250000000LL, "Worker watchdog exceeded 50ms+200ms fixture budget");
        qInfo() << "Worker flow-fault to peer reset ms" << (reset - faultAt->load()) / 1000000;
        QVERIFY(noResumeOrAccessoryOff(journal));
        const auto probe = ControllerRuntimeTestAccess::probe(runtime);
        QVERIFY(probe && !probe->lease && !probe->job && probe->releaseRequired);
        QVERIFY(!probe->extractionError.isEmpty()); // Later Flowing does not clear the fault.
        QTRY_VERIFY_WITH_TIMEOUT(guiDelivered, 500);
    }
    void flowExpiryIsIndependentOfGuiAndCommandAcks() {
        RuntimePeer peer;
        QVERIFY(startPeer(peer));
        RuntimeOwner owner;
        auto &runtime = *owner.runtime;
        QVERIFY(open(runtime, peer));
        QTRY_VERIFY_WITH_TIMEOUT(runtime.status().ready, 5000);
        auto flowAt = std::make_shared<std::atomic<qint64>>(-1);
        QVERIFY(ControllerRuntimeTestAccess::commission(runtime, flowAt));
        QVERIFY(runtime.requestStart(edge()).enqueued);
        QTRY_COMPARE_WITH_TIMEOUT(countCommand(events(peer), "M4 S1000"), 1, 3000);
        auto probe = ControllerRuntimeTestAccess::probe(runtime);
        QVERIFY(probe);
        const auto age = probe->clockMs - probe->flowAtMs;
        QVERIFY(age >= 0 && age < 2400);
        QTest::qWait(int(2500 - age));
        const auto begin = RuntimePeer::monotonicNs();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        const auto end = RuntimePeer::monotonicNs();
        const auto journal = events(peer);
        const auto reset = firstResetAfter(journal, begin);
        QVERIFY(reset > begin && reset < end);
        QVERIFY(reset >= flowAt->load() + 2995000000LL); // Cannot be masked by an earlier unrelated policy.
        QVERIFY(reset <= flowAt->load() + 3250000000LL);
        qInfo() << "Flow observation age at peer reset ms" << (reset - flowAt->load()) / 1000000;
        QVERIFY(noResumeOrAccessoryOff(journal));
    }
    void statusExpiryIsIndependentOfGuiWithHealthyFlow() {
        RuntimePeer peer;
        QVERIFY(startPeer(peer));
        RuntimeOwner owner;
        auto &runtime = *owner.runtime;
        QVERIFY(open(runtime, peer));
        QTRY_VERIFY_WITH_TIMEOUT(runtime.status().ready, 5000);
        QVERIFY(ControllerRuntimeTestAccess::commission(runtime));
        QVERIFY(ControllerRuntimeTestAccess::refreshFlow(runtime));
        QVERIFY(runtime.requestStart(edge()).enqueued);
        QTRY_COMPARE_WITH_TIMEOUT(countCommand(events(peer), "M4 S1000"), 1, 3000);
        QVERIFY(peer.control({{"action", "set"}, {"values", QJsonObject{{"statusEnabled", false}}}}).ok);
        const auto probe = ControllerRuntimeTestAccess::probe(runtime);
        QVERIFY(probe && probe->statusAtNs > 0);
        const auto begin = RuntimePeer::monotonicNs();
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        const auto end = RuntimePeer::monotonicNs();
        const auto journal = events(peer);
        const auto reset = firstResetAfter(journal, begin);
        QVERIFY(reset > begin && reset < end);
        QVERIFY(reset >= probe->statusAtNs + 1495000000LL);
        QVERIFY(reset <= probe->statusAtNs + 1750000000LL);
        qInfo() << "Status observation age at peer reset ms" << (reset - probe->statusAtNs) / 1000000;
        QVERIFY(noResumeOrAccessoryOff(journal));
    }
    void missingAcksCannotBeKeptAliveByUnrelatedHealthyStatus() {
        RuntimePeer peer;
        QVERIFY(startPeer(peer));
        RuntimeOwner owner;
        auto &runtime = *owner.runtime;
        QVERIFY(open(runtime, peer));
        QTRY_VERIFY_WITH_TIMEOUT(runtime.status().ready, 5000);
        QVERIFY(ControllerRuntimeTestAccess::commission(runtime));
        QVERIFY(ControllerRuntimeTestAccess::refreshFlow(runtime));
        QVERIFY(runtime.requestStart(streamingJob()).enqueued);
        QTRY_VERIFY_WITH_TIMEOUT(totalCommands(peer) >= 20, 3000);
        QVERIFY(peer.control({{"action", "set"}, {"values", QJsonObject{{"ackEnabled", false}}}}).ok);
        const auto before = events(peer);
        qint64 lastAckAt = -1;
        for (const auto &value : before) {
            const auto event = value.toObject();
            if (event.value("kind") == "ack") lastAckAt = event.value("atNs").toInteger();
        }
        QVERIFY(lastAckAt > 0);
        QTest::qWait(100); // Drain already-sent ACKs; the peer will send no new ones.
        const auto probe = ControllerRuntimeTestAccess::probe(runtime);
        QVERIFY(probe && probe->job && probe->lease && probe->awaitingAck && probe->extractionError.isEmpty());
        QVERIFY(probe->ackDueAtNs > RuntimePeer::monotonicNs() + 9000000000LL);
        QTest::qWait(9100); // GUI responsive until shortly before the ACK-specific bound.
        const auto healthy = ControllerRuntimeTestAccess::probe(runtime);
        QVERIFY(healthy && healthy->job && healthy->awaitingAck && healthy->extractionError.isEmpty());
        QVERIFY(runtime.status().statusFresh);
        const auto begin = RuntimePeer::monotonicNs();
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        const auto end = RuntimePeer::monotonicNs();
        const auto journal = events(peer);
        const auto reset = firstResetAfter(journal, begin);
        QVERIFY2(reset >= begin && reset < end, "Independent ACK deadline did not stop before GUI return; status must not renew ACK age");
        // The oldest unacknowledged WRITE may precede the last received ACK
        // in a pipelined window. Use its copied private absolute deadline for
        // the lower bound, plus a separate peer-ACK upper bound.
        QVERIFY(reset >= probe->ackDueAtNs - 5000000LL);
        QVERIFY(reset <= probe->ackDueAtNs + 250000000LL);
        QVERIFY(reset <= lastAckAt + 10250000000LL);
        qInfo() << "ACK deadline to peer reset ms" << (reset - probe->ackDueAtNs) / 1000000;
        QVERIFY(noResumeOrAccessoryOff(journal));
    }
    void stopLatchCancelsPreparedStartBeforeAnyMotion() {
        RuntimePeer peer;
        QVERIFY(startPeer(peer));
        RuntimeOwner owner;
        auto &runtime = *owner.runtime;
        QVERIFY(open(runtime, peer));
        QTRY_VERIFY_WITH_TIMEOUT(runtime.status().ready, 5000);
        QVERIFY(ControllerRuntimeTestAccess::commission(runtime));
        const auto before = ControllerRuntimeTestAccess::probe(runtime);
        QVERIFY(before);
        auto barrier = ControllerRuntimeTestAccess::hold(runtime);
        QVERIFY(barrier);
        const auto begin = RuntimePeer::monotonicNs();
        QVERIFY(runtime.requestStart(edge()).enqueued);
        QElapsedTimer response;
        response.start();
        runtime.stop();
        QVERIFY(response.elapsed() < 100); // Cannot require worker/GUI roundtrip.
        barrier->release();
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        const auto end = RuntimePeer::monotonicNs();
        const auto journal = events(peer);
        QVERIFY(firstResetAfter(journal, begin) < end);
        QVERIFY(firstResetAfter(journal, begin) >= begin);
        QVERIFY(noUnsafeCommandAfter(journal, begin));
        const auto after = ControllerRuntimeTestAccess::probe(runtime);
        QVERIFY(after && after->generation > before->generation && !after->lease && !after->job);
        QVERIFY(!barrier->timedOut());
    }
    void queuedMotionExpiresBeforeDispatch() {
        RuntimePeer peer;
        QVERIFY(startPeer(peer));
        RuntimeOwner owner;
        auto &runtime = *owner.runtime;
        QVERIFY(open(runtime, peer));
        QTRY_VERIFY_WITH_TIMEOUT(runtime.status().ready, 5000);
        QVERIFY(ControllerRuntimeTestAccess::commission(runtime));
        runtime.jog(.25, 0);
        QTRY_VERIFY_WITH_TIMEOUT(countCommand(events(peer), "$J=G91 G21 X0.250 Y0.000 F600") == 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(runtime.status().ready, 3000);
        auto barrier = ControllerRuntimeTestAccess::hold(runtime);
        QVERIFY(barrier);
        const auto begin = RuntimePeer::monotonicNs();
        runtime.jog(.75, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(400)); // Deliberately past 250-ms request TTL.
        barrier->release();
        QVERIFY(ControllerRuntimeTestAccess::probe(runtime)); // Worker FIFO fence after drain.
        QTRY_VERIFY_WITH_TIMEOUT(!runtime.status().requestPending && runtime.status().ready, 1000);
        QVERIFY(noUnsafeCommandAfter(events(peer), begin));
        QVERIFY(!barrier->timedOut());
    }
    void closedConnectionAndOldRevocationCannotReplayMotion() {
        RuntimePeer first, second;
        QVERIFY(startPeer(first)); QVERIFY(startPeer(second));
        RuntimeOwner owner;
        auto &runtime = *owner.runtime;
        QVERIFY(open(runtime, first, 'a'));
        QTRY_VERIFY_WITH_TIMEOUT(runtime.status().ready, 5000);
        QVERIFY(ControllerRuntimeTestAccess::commission(runtime));
        const auto old = ControllerRuntimeTestAccess::probe(runtime);
        QVERIFY(old);
        runtime.close();
        QTRY_VERIFY_WITH_TIMEOUT(!runtime.status().connected && !runtime.status().occupied, 3000);
        QVERIFY(open(runtime, second, 'b'));
        QTRY_VERIFY_WITH_TIMEOUT(runtime.status().ready, 5000);
        QVERIFY(ControllerRuntimeTestAccess::commission(runtime));
        const auto current = ControllerRuntimeTestAccess::probe(runtime);
        QVERIFY(current && current->epoch > old->epoch && current->generation > old->generation);
        const auto begin = RuntimePeer::monotonicNs();
        QVERIFY(ControllerRuntimeTestAccess::injectStampedJog(runtime, old->generation, current->epoch));
        QVERIFY(ControllerRuntimeTestAccess::probe(runtime));
        QTRY_VERIFY_WITH_TIMEOUT(!runtime.status().requestPending && runtime.status().ready, 1000);
        QVERIFY(noUnsafeCommandAfter(events(second), begin));
        QVERIFY(ControllerRuntimeTestAccess::injectStampedJog(runtime, current->generation, old->epoch));
        QVERIFY(ControllerRuntimeTestAccess::probe(runtime));
        QTRY_VERIFY_WITH_TIMEOUT(!runtime.status().requestPending && runtime.status().ready, 1000);
        QVERIFY(noUnsafeCommandAfter(events(second), begin));
        runtime.jog(.5, 0); // Fresh intent with new generation remains possible.
        QTRY_COMPARE_WITH_TIMEOUT(countCommand(events(second), "$J=G91 G21 X0.500 Y0.000 F600"), 1, 3000);
    }
    void destructionRevokesActiveLeaseAndStopsItsWorker() {
        RuntimePeer peer;
        QVERIFY(startPeer(peer));
        RuntimeOwner owner;
        auto &runtime = owner.runtime;
        QVERIFY(open(*runtime, peer));
        QTRY_VERIFY_WITH_TIMEOUT(runtime->status().ready, 5000);
        QVERIFY(ControllerRuntimeTestAccess::commission(*runtime));
        QVERIFY(ControllerRuntimeTestAccess::refreshFlow(*runtime));
        QVERIFY(runtime->requestStart(edge()).enqueued);
        QTRY_COMPARE_WITH_TIMEOUT(countCommand(events(peer), "M4 S1000"), 1, 3000);
        auto destroyed = ControllerRuntimeTestAccess::observeWorkerDestruction(*runtime);
        QVERIFY(destroyed);
        const auto begin = RuntimePeer::monotonicNs();
        runtime.reset();
        QTRY_VERIFY_WITH_TIMEOUT(destroyed->load(), 2500);
        const auto journal = events(peer);
        QVERIFY(firstResetAfter(journal, begin) >= begin);
        QVERIFY(noResumeOrAccessoryOff(journal));
    }
};
QTEST_MAIN(ControllerRuntimeAdversarialTest)
#include "controllerruntime_adversarial_test.moc"
