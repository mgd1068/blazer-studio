#include "controllerruntime.h"
#include "controllerruntimemailbox.h"
#include "controllerruntimeworker.h"
#include "job.h"
#include <QCoreApplication>
#include <QMutexLocker>
#include <QRandomGenerator>
#include <QScopeGuard>
#include <QThreadPool>
#include <chrono>
#include <deque>

namespace {
qint64 nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace
using Kind = ControllerRuntimeRequestKind;
using Request = ControllerRuntimeRequest;
namespace {
QThreadPool &retirementPool() {
    static QThreadPool pool;
    static const bool configured = [] {
        pool.setMaxThreadCount(1);
        return true;
    }();
    Q_UNUSED(configured);
    return pool;
}
} // namespace
void retireControllerJob(PreparedControllerJob::Ptr job) {
    if (job)
        retirementPool().start([job = std::move(job)] {});
}
void retireControllerRequests(std::deque<ControllerRuntimeRequest> requests) {
    if (!requests.empty())
        retirementPool().start([requests = std::move(requests)] {});
}
class ControllerRuntimeHost {
  public:
    QThread *thread = nullptr;
    ControllerRuntimeWorker *worker = nullptr;
};
ControllerRuntimeSession::ControllerRuntimeSession(DeviceProfile profile, QObject *parent)
    : ControllerSession(parent), m_profile(std::move(profile)),
      m_host(std::make_shared<ControllerRuntimeHost>()),
      m_mailbox(std::make_shared<ControllerRuntimeMailbox>()) {
    (void)retirementPool(); // Construct pool owner in the GUI thread, never on the realtime path.
    m_mailbox->inputIssuer = QRandomGenerator::global()->generate64();
    auto host = m_host;
    host->thread = new QThread;
    m_worker = host->worker = new ControllerRuntimeWorker(m_profile, m_mailbox);
    m_worker->moveToThread(host->thread);
    {
        QMutexLocker lock(&m_mailbox->mutex);
        m_mailbox->context = m_worker;
        m_mailbox->guiContext = this;
    }
    connect(host->thread, &QThread::started, m_worker,
            [worker = m_worker] { worker->initialize(); });
    connect(m_worker, &ControllerRuntimeWorker::latestAvailable, this,
            &ControllerRuntimeSession::consumeLatest, Qt::QueuedConnection);
    connect(
        m_worker, &ControllerRuntimeWorker::openFinished, this,
        [this](quint64 id, bool opened, const QString &reason) {
            if (id != m_pendingOpen)
                return;
            m_pendingOpen = 0;
            emit connectionRequestFinished(id, opened, reason);
            consumeLatest();
        },
        Qt::QueuedConnection);
    connect(host->thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(host->thread, &QThread::finished, host->thread, [host] {
        auto thread = host->thread;
        host->worker = nullptr;
        host->thread = nullptr;
        thread->deleteLater();
    });
    // Last-resort process teardown is bounded and has no GUI callback dependency.
    if (auto app = QCoreApplication::instance()) {
        std::weak_ptr<ControllerRuntimeHost> weak = host;
        auto mailbox = m_mailbox;
        connect(app, &QCoreApplication::aboutToQuit, host->thread, [weak, mailbox] {
            if (auto owner = weak.lock(); owner && owner->thread && owner->thread->isRunning()) {
                mailbox->revoke(true, true);
                owner->thread->wait(1500);
            }
        });
    }
    host->thread->start();
}
ControllerRuntimeSession::~ControllerRuntimeSession() {
    {
        QMutexLocker lock(&m_mailbox->mutex);
        m_mailbox->guiContext = nullptr;
        if (m_mailbox->prepareCancel)
            m_mailbox->prepareCancel->store(true);
    }
    m_mailbox->revoke(true, true);
}
ControllerStatus ControllerRuntimeSession::status() const {
    QMutexLocker lock(&m_mailbox->mutex);
    auto result = m_mailbox->latest;
    const auto now = nowMs();
    result.statusAgeMs = m_mailbox->deviceCapture < 0 ? -1 : now - m_mailbox->deviceCapture;
    result.runtimeHeartbeatAgeMs =
        m_mailbox->heartbeatCapture < 0 ? -1 : now - m_mailbox->heartbeatCapture;
    result.statusFresh = result.statusAgeMs >= 0 && result.statusAgeMs <= 1500 &&
                         result.runtimeHeartbeatAgeMs >= 0 && result.runtimeHeartbeatAgeMs <= 1000;
    if (!result.statusFresh) {
        result.ready = result.verified = result.overridesReady = false;
        if (result.motionSafetyError.isEmpty())
            result.motionSafetyError = "Aktueller Runtime-/Controllerstatus fehlt.";
    }
    result.closing |= m_closing || m_mailbox->close.load();
    result.opening |= m_pendingOpen != 0;
    result.requestPending |= m_pendingOpen != 0 || !m_mailbox->normal.empty();
    result.occupied |= result.opening || result.requestPending || m_mailbox->close.load() ||
                       m_mailbox->stop.load();
    result.occupied |= result.closing;
    result.busy |= result.occupied;
    if (result.closing)
        result.connectionPhase = "closing";
    else if (m_pendingOpen)
        result.connectionPhase = "opening";
    if (result.requestPending || result.opening)
        result.ready = false;
    return result;
}
void ControllerRuntimeSession::consumeLatest() {
    {
        QMutexLocker lock(&m_mailbox->mutex);
        m_mailbox->notificationPending = false;
        if (m_closing && !m_mailbox->close.load() && !m_mailbox->latest.connected &&
            !m_mailbox->latest.occupied)
            m_closing = false;
    }
    emit changed();
}
QStringList ControllerRuntimeSession::logEntries() const {
    QMutexLocker lock(&m_mailbox->mutex);
    return m_mailbox->log;
}
QVariantMap ControllerRuntimeSession::diagnosticSnapshot() const {
    QMutexLocker lock(&m_mailbox->mutex);
    return m_mailbox->diagnostic;
}
ControllerSubmission ControllerRuntimeSession::requestOpen(const QString &endpoint,
                                                           const QByteArray &binding) {
    if (endpoint.isEmpty() || status().occupied || status().connected)
        return {0, false, "Verbindung bereits belegt oder Anschluss fehlt."};
    const auto id = ++m_nextRequest;
    Request request{Kind::Open};
    request.id = id;
    request.endpoint = endpoint;
    request.binding = binding;
    request.issuedAt = nowMs();
    request.expiresAt = request.issuedAt + 3000;
    if (!m_mailbox->enqueue(std::move(request)))
        return {id, false, "Runtime-Anforderung nicht eingereiht."};
    m_pendingOpen = id;
    emit changed();
    return {id, true, {}};
}
bool ControllerRuntimeSession::open(const QString &endpoint) {
    return requestOpen(endpoint, {}).enqueued;
}
void ControllerRuntimeSession::cancelPendingOpen() {
    const auto cancelled = m_pendingOpen;
    m_pendingOpen = 0;
    if (cancelled)
        QMetaObject::invokeMethod(
            this,
            [this, cancelled] {
                emit connectionRequestFinished(cancelled, false,
                                               "Verbindungsanforderung abgebrochen.");
            },
            Qt::QueuedConnection);
}
void ControllerRuntimeSession::close() {
    m_closing = true;
    cancelPendingOpen();
    m_mailbox->revoke(true);
    emit changed();
}
void ControllerRuntimeSession::stop() {
    cancelPendingOpen();
    m_mailbox->revoke(false);
    emit changed();
}
namespace {
void send(const std::shared_ptr<ControllerRuntimeMailbox> &box, Request request) {
    request.issuedAt = nowMs();
    request.expiresAt = request.issuedAt + 250;
    box->enqueue(std::move(request));
}
} // namespace
void ControllerRuntimeSession::jog(double x, double y) {
    Request r{Kind::Jog};
    r.x = x;
    r.y = y;
    send(m_mailbox, std::move(r));
    emit changed();
}
void ControllerRuntimeSession::home() {
    send(m_mailbox, Request{Kind::Home});
    emit changed();
}
void ControllerRuntimeSession::zero() {
    send(m_mailbox, Request{Kind::Zero});
    emit changed();
}
void ControllerRuntimeSession::frame(const QVariantMap &b) {
    Request r{Kind::Frame};
    r.bounds = b;
    send(m_mailbox, std::move(r));
    emit changed();
}
void ControllerRuntimeSession::pause() {
    send(m_mailbox, Request{Kind::Pause});
    emit changed();
}
bool ControllerRuntimeSession::overrides(int feed, int power) {
    Request r{Kind::Override};
    r.feed = feed;
    r.power = power;
    send(m_mailbox, std::move(r));
    emit changed();
    return true;
}
void ControllerRuntimeSession::focus(bool on) {
    if (!on)
        endFocus(); /* No legacy unproven Begin. */
}
void ControllerRuntimeSession::beginFocus(const ControllerRuntimeInputStamp &stamp) {
    Request r{Kind::Focus};
    r.input = stamp;
    send(m_mailbox, std::move(r));
    emit changed();
}
void ControllerRuntimeSession::endFocus() {
    m_mailbox->focusGeneration.fetch_add(1);
    m_mailbox->focusOff.store(true);
    {
        QMutexLocker lock(&m_mailbox->mutex);
        m_mailbox->wakeLocked();
    }
    emit changed();
}
ControllerRuntimeInputStamp
ControllerRuntimeSession::sealNativePress(quint64 interaction, quint64 window, qint64 observedAt) {
    ControllerRuntimeInputStamp stamp;
    const auto now = nowMs();
    if (!interaction || !window || observedAt <= 0 || observedAt > now || now - observedAt > 250)
        return stamp;
    stamp.interactionId = interaction;
    stamp.windowEpoch = window;
    stamp.observedAtMs = observedAt;
    stamp.expiresAtMs = observedAt + 250;
    stamp.proof = std::shared_ptr<const ControllerNativePressProof>(new ControllerNativePressProof(
        m_mailbox->inputIssuer, interaction, window, observedAt, stamp.expiresAtMs));
    return stamp;
}
void ControllerRuntimeSession::revokeFocusContext() {
    m_mailbox->focusWindowEpoch.fetch_add(1);
    endFocus();
}
bool ControllerRuntimeSession::jobPreparing() const {
    QMutexLocker lock(&m_mailbox->mutex);
    return m_mailbox->preparing;
}
bool ControllerRuntimeSession::jobPrepared() const {
    QMutexLocker lock(&m_mailbox->mutex);
    return !m_mailbox->preparing && m_mailbox->prepared && m_mailbox->prepared->valid();
}
QString ControllerRuntimeSession::prepareReason() const {
    QMutexLocker lock(&m_mailbox->mutex);
    if (m_mailbox->preparing)
        return "Controllerausgabe wird vorbereitet; noch keine Startfreigabe.";
    if (!m_mailbox->prepareError.isEmpty())
        return m_mailbox->prepareError;
    return m_mailbox->prepared && m_mailbox->prepared->valid()
               ? QString()
               : QString("Controllerausgabe noch nicht vorbereitet.");
}
void ControllerRuntimeSession::prepare(const JobPlan &plan) {
    const auto profile = plan.deviceProfile().outputFingerprint();
    if (plan.valid() && m_candidateMoves.constData() == plan.moves.constData() &&
        m_candidateMoves.size() == plan.moves.size() && m_candidateProfile == profile &&
        (jobPreparing() || jobPrepared()))
        return;
    m_candidateMoves = plan.moves;
    m_candidateProfile = profile;
    const auto sequence = ++m_prepareSequence;
    auto cancel = std::make_shared<std::atomic_bool>(false);
    auto mailbox = m_mailbox;
    {
        QMutexLocker lock(&mailbox->mutex);
        if (mailbox->prepareCancel)
            mailbox->prepareCancel->store(true);
        mailbox->prepareCancel = cancel;
        mailbox->prepareSequence = sequence;
        mailbox->preparing = true;
        mailbox->prepareError.clear();
        mailbox->prepared.reset();
    }
    emit changed();
    QThreadPool::globalInstance()->start([mailbox, plan, cancel, sequence] {
        auto prepared = PreparedControllerJob::prepare(plan, cancel);
        QMutexLocker lock(&mailbox->mutex);
        if (mailbox->prepareSequence != sequence || isCancelled(cancel) || !mailbox->guiContext)
            return;
        mailbox->prepared = std::move(prepared);
        mailbox->preparing = false;
        mailbox->prepareError =
            mailbox->prepared->valid() ? QString() : mailbox->prepared->errors().join("; ");
        QMetaObject::invokeMethod(
            mailbox->guiContext,
            [context = mailbox->guiContext] {
                static_cast<ControllerRuntimeSession *>(context)->consumeLatest();
            },
            Qt::QueuedConnection);
    });
}
ControllerSubmission ControllerRuntimeSession::requestStart(PreparedControllerJob::Ptr prepared) {
    const auto id = ++m_nextRequest;
    if (!prepared || !prepared->valid())
        return {id, false, "Vorbereitete Ausgabe fehlt."};
    if (status().occupied || !status().ready)
        return {id, false, "Runtime nicht im geprüften freien Leerlauf."};
    Request request{Kind::Start};
    request.id = id;
    request.prepared = std::move(prepared);
    request.issuedAt = nowMs();
    request.expiresAt = request.issuedAt + 250;
    if (!m_mailbox->enqueue(std::move(request)))
        return {id, false, "Startanforderung nicht eingereiht."};
    emit changed();
    return {id, true, {}};
}
void ControllerRuntimeSession::start(const JobPlan &plan) {
    if (!plan.valid() || m_candidateMoves.constData() != plan.moves.constData() ||
        m_candidateMoves.size() != plan.moves.size() ||
        m_candidateProfile != plan.deviceProfile().outputFingerprint() || !jobPrepared()) {
        prepare(plan);
        return; // Never auto-start from an asynchronous preparation completion.
    }
    PreparedControllerJob::Ptr job;
    {
        QMutexLocker lock(&m_mailbox->mutex);
        job = m_mailbox->prepared;
    }
    requestStart(std::move(job));
}

ControllerRuntimeWorker::ControllerRuntimeWorker(DeviceProfile profile,
                                                 std::shared_ptr<ControllerRuntimeMailbox> mailbox)
    : m_profile(std::move(profile)), m_mailbox(std::move(mailbox)) {
}
ControllerRuntimeWorker::~ControllerRuntimeWorker() = default;
void ControllerRuntimeWorker::initialize() {
    Q_ASSERT(QThread::currentThread() == thread());
    m_accessoryClock.start();
    m_core = std::make_unique<GrblSession>(nullptr, m_profile);
    m_core->serial.setReadBufferSize(65536);
    m_core->serial.setWriteBufferSize(256);
    m_watchdog = new QTimer(this);
    m_watchdog->setInterval(50);
    connect(m_watchdog, &QTimer::timeout, this, &ControllerRuntimeWorker::watchdog);
    m_core->dispatchCancelled = [this] {
        return m_mailbox->stop.load() || m_mailbox->close.load() || m_mailbox->shutdown.load();
    };
    m_core->dispatchPreflight = [this] { return cancelDispatch(); };
    connect(m_core.get(), &ControllerSession::changed, this, &ControllerRuntimeWorker::coreChanged);
    m_watchdog->start();
    publish();
}
void ControllerRuntimeWorker::drain() {
    if (!m_core)
        return;
    {
        QMutexLocker lock(&m_mailbox->mutex);
        m_mailbox->wakePending = false;
    }
    if (m_mailbox->focusOff.exchange(false) && (m_focusActive || m_core->firing))
        m_core->focus(false);
    if (m_mailbox->stop.exchange(false)) {
        m_accessory.transition(AccessoryContract::Event::Fault);
        m_jobActive = m_focusActive = false;
        if (!m_mailbox->close.load() && !m_core->awaitingReset)
            m_core->stop();
        retirePrepared();
    }
    if (m_mailbox->close.exchange(false)) {
        m_accessory.transition(AccessoryContract::Event::Disconnect);
        m_core->close();
        ++m_connectionEpoch;
    }
    if (m_mailbox->shutdown.load()) {
        shutdown();
        return;
    }
    Request request{Kind::Home};
    bool have = false;
    {
        QMutexLocker lock(&m_mailbox->mutex);
        if (!m_mailbox->normal.empty()) {
            request = std::move(m_mailbox->normal.front());
            m_mailbox->normal.pop_front();
            have = true;
        }
    }
    const auto retireRequest =
        qScopeGuard([&request] { retireControllerJob(std::move(request.prepared)); });
    if (have && request.generation == m_mailbox->generation.load() &&
        nowMs() <= request.expiresAt &&
        (request.kind == Kind::Open || request.connectionEpoch == m_connectionEpoch)) {
        switch (request.kind) {
        case Kind::Open: {
            ++m_connectionEpoch;
            m_accessory = AccessoryContract(true);
            const bool opened = m_core->open(request.endpoint);
            emit openFinished(
                request.id, opened,
                opened ? QString() : QString("Controlleranschluss konnte nicht geöffnet werden."));
            break;
        }
        case Kind::Jog:
            m_core->jog(request.x, request.y);
            break;
        case Kind::Home:
            m_core->home();
            break;
        case Kind::Zero:
            if (!m_accessory.activeLease() && !m_jobActive && !m_focusActive)
                m_core->zero();
            break;
        case Kind::Frame:
            m_core->frame(request.bounds);
            break;
        case Kind::Pause:
            if (m_core->paused && !m_accessory.preflightError(m_accessoryClock.elapsed()).isEmpty())
                m_core->stop();
            else
                m_core->pause();
            break;
        case Kind::Override:
            if (m_accessory.preflightError(m_accessoryClock.elapsed()).isEmpty())
                m_core->overrides(request.feed, request.power);
            break;
        case Kind::Focus: {
            const auto &proof = request.input.proof;
            if (!proof || proof->issuer != m_mailbox->inputIssuer ||
                proof->window != m_mailbox->focusWindowEpoch.load() ||
                proof->interaction <= m_lastFocusInteraction || proof->observedAt > nowMs() ||
                proof->expiresAt < nowMs() ||
                request.focusGeneration != m_mailbox->focusGeneration.load() || !m_core->ready() ||
                !m_core->verified() || !m_core->positionReferenced ||
                !m_core->motionSafetyError().isEmpty())
                break;
            if (!m_accessory.begin(m_accessoryClock.elapsed()))
                break;
            m_lastFocusInteraction = proof->interaction;
            m_focusActive = true;
            m_core->focus(true);
            if (!m_core->firing) {
                m_focusActive = false;
                m_accessory.transition(AccessoryContract::Event::Complete);
            }
            break;
        }
        case Kind::FocusOff:
            m_core->focus(false);
            break;
        case Kind::Start:
            if (!m_core->ready() || !m_core->verified() || !m_core->positionReferenced ||
                !m_core->motionSafetyError().isEmpty())
                break;
            if (!m_accessory.begin(m_accessoryClock.elapsed()))
                break;
            m_jobActive = true;
            m_activePrepared = std::move(request.prepared);
            if (!m_core->startPrepared(*m_activePrepared)) {
                m_jobActive = false;
                m_accessory.transition(AccessoryContract::Event::Complete);
                retirePrepared();
            }
            break;
        }
    } else if (have && request.kind == Kind::Open) {
        emit openFinished(request.id, false, "Verbindungsanforderung veraltet oder widerrufen.");
    }
    publish();
    QMutexLocker lock(&m_mailbox->mutex);
    if (!m_mailbox->normal.empty() || m_mailbox->stop.load() || m_mailbox->close.load() ||
        m_mailbox->focusOff.load())
        m_mailbox->wakeLocked();
}
void ControllerRuntimeWorker::retirePrepared() {
    if (!m_activePrepared)
        return;
    retireControllerJob(std::move(m_activePrepared));
}
bool ControllerRuntimeWorker::cancelDispatch() {
    if (m_mailbox->stop.load() || m_mailbox->close.load() || m_mailbox->shutdown.load())
        return true;
    if (!m_core || m_core->handshaking || m_core->awaitingReset || m_core->homingPending)
        return false;
    const auto status = m_core->status();
    const bool output = m_accessory.activeLease() || m_jobActive || m_focusActive || status.firing;
    if (status.coordinateUpdatePending && !output)
        return false;
    if (!status.verified || !status.positionReferenced || !status.motionSafetyError.isEmpty() ||
        (output && !m_accessory.preflightError(m_accessoryClock.elapsed()).isEmpty())) {
        m_mailbox->revoke(false);
        return true;
    }
    return false;
}
void ControllerRuntimeWorker::coreChanged() {
    if (!m_core)
        return;
    const auto status = m_core->status();
    const bool output = m_accessory.activeLease() || m_jobActive || m_focusActive || status.firing;
    const bool lost =
        (m_wasVerified && !status.verified) || (m_wasReferenced && !status.positionReferenced);
    m_wasVerified = status.verified;
    m_wasReferenced = status.positionReferenced;
    if (!status.connected) {
        m_accessory.transition(AccessoryContract::Event::Disconnect);
        m_jobActive = m_focusActive = false;
        retirePrepared();
    } else if (lost && !(status.coordinateUpdatePending && !output)) {
        m_accessory.transition(AccessoryContract::Event::Fault);
        const bool mustStop =
            (status.executing && !status.referencing && !status.resetting) || output;
        if (status.resetting && !status.firing) {
            m_jobActive = m_focusActive = false;
            retirePrepared();
        } else if (mustStop) {
            m_jobActive = m_focusActive = false;
            m_core->stop();
            retirePrepared();
            return;
        }
    } else if ((m_jobActive || m_focusActive) && !status.executing && !status.firing) {
        m_jobActive = m_focusActive = false;
        m_accessory.transition(AccessoryContract::Event::Complete);
        retirePrepared();
    }
    publish();
}
void ControllerRuntimeWorker::watchdog() {
    if (!m_core)
        return;
    const auto status = m_core->status();
    const bool output = m_accessory.activeLease() || m_jobActive || m_focusActive || status.firing;
    const bool coordinate = status.coordinateUpdatePending && !output;
    const bool motion =
        status.executing && !status.referencing && !status.resetting && !coordinate &&
        (!status.verified || !status.positionReferenced || !status.motionSafetyError.isEmpty());
    const bool unsafeOutput =
        output &&
        (!status.verified || !status.positionReferenced || !status.motionSafetyError.isEmpty() ||
         !m_accessory.preflightError(m_accessoryClock.elapsed()).isEmpty());
    if (motion || unsafeOutput) {
        m_accessory.transition(AccessoryContract::Event::Fault);
        m_jobActive = m_focusActive = false;
        m_core->stop();
    }
    if (!status.connected) {
        m_accessory.transition(AccessoryContract::Event::Disconnect);
        m_jobActive = m_focusActive = false;
    }
    publish();
}
void ControllerRuntimeWorker::publish() {
    if (!m_core)
        return;
    auto status = m_core->status();
    const auto now = nowMs();
    status.connectionEpoch = m_connectionEpoch;
    status.sequence = ++m_sequence;
    status.opening = m_core->awaitingReset || m_core->handshaking;
    status.accessoryLeaseActive = m_accessory.activeLease();
    status.accessoryJobActive = m_jobActive;
    status.accessoryFocusActive = m_focusActive;
    status.extractionSafetyError = m_accessory.preflightError(m_accessoryClock.elapsed());
    status.occupied = status.busy || status.firing || status.opening ||
                      status.accessoryLeaseActive || m_jobActive || m_focusActive;
    status.connectionPhase = !status.connected       ? QString("closed")
                             : m_core->handshaking   ? QString("handshake")
                             : m_core->awaitingReset ? QString("opening")
                             : status.occupied       ? QString("active")
                                                     : QString("idle");
    const auto diagnostic = m_core->diagnosticSnapshot();
    QMutexLocker lock(&m_mailbox->mutex);
    m_mailbox->latest = status;
    m_mailbox->deviceCapture = m_core->lastStatus.isValid() && m_core->knownPosition &&
                                       m_core->knownOffset && m_core->knownOutput &&
                                       m_core->reportMm
                                   ? now - m_core->lastStatus.elapsed()
                                   : -1;
    m_mailbox->heartbeatCapture = now;
    m_mailbox->log = m_core->logEntries();
    m_mailbox->diagnostic = diagnostic;
    if (!m_mailbox->notificationPending && m_mailbox->guiContext) {
        m_mailbox->notificationPending = true;
        QMetaObject::invokeMethod(
            m_mailbox->guiContext,
            [context = m_mailbox->guiContext] {
                static_cast<ControllerRuntimeSession *>(context)->consumeLatest();
            },
            Qt::QueuedConnection);
    }
}
void ControllerRuntimeWorker::shutdown() {
    if (m_watchdog)
        m_watchdog->stop();
    if (m_core) {
        m_core->close();
        retirePrepared();
        m_core.reset();
    }
    std::deque<ControllerRuntimeRequest> retired;
    {
        QMutexLocker lock(&m_mailbox->mutex);
        m_mailbox->accepting = false;
        m_mailbox->context = nullptr;
        m_mailbox->normal.swap(retired);
    }
    retireControllerRequests(std::move(retired));
    emit shutdownFinished();
    QThread::currentThread()->quit();
}
