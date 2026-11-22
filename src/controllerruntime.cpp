#include "controllerruntime.h"
#include "controllerruntimemailbox.h"
#include "controllerruntimeworker.h"
#include "job.h"
#include <QCoreApplication>
#include <QMutexLocker>
#include <QRandomGenerator>
#include <QPointer>
#include <QScopeGuard>
#include <QSignalBlocker>
#include <QThreadPool>
#include <chrono>
#include <deque>
#include <algorithm>
#include <time.h>

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
        pool.setExpiryTimeout(-1);
        pool.start([] {}); // Warm thread creation before any controller I/O is admitted.
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
void retireLocalJob(PreparedLocalJob::Ptr job) {
    if(job)retirementPool().start([job=std::move(job)]{});
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
    m_preparation = new ControllerJobPreparation(this);
    connect(m_preparation, &ControllerJobPreparation::changed, this, [this] { emit changed(); });
    connect(m_preparation,&ControllerJobPreparation::finished,this,[this](quint64 generation){
        if(generation!=m_prepareSequence || m_candidateLocalFrame.isEmpty())return;
        const auto local=m_preparation->localResult(generation);
        Request request{Kind::LocalData};request.bounds={{"op","candidate"},{"frameId",QString::fromLatin1(m_candidateLocalFrame)}};
        request.localPrepared=local;request.id=++m_nextRequest;
        if(!local || !local->valid())request.bounds["error"]=m_preparation->reason();
        request.issuedAt=nowMs();request.expiresAt=request.issuedAt+250;m_mailbox->enqueue(std::move(request));
    });
    m_uiAgeTicker = new QTimer(this);
    m_uiAgeTicker->setInterval(100);
    connect(m_uiAgeTicker, &QTimer::timeout, this, [this] { emit changed(); });
    m_uiAgeTicker->start();
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
            const QPointer<ControllerRuntimeSession> alive(this);
            emit connectionRequestFinished(id, opened, reason);
            if (!alive)
                return;
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
    const QSignalBlocker notifications(this);
    {
        QMutexLocker lock(&m_mailbox->mutex);
        m_mailbox->guiContext = nullptr;
    }
    m_mailbox->revoke(true, true); // Revoke I/O BEFORE any large data-side cleanup.
    m_preparation->close();
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
        result.holdConfirmed=false;
        if (result.motionSafetyError.isEmpty())
            result.motionSafetyError = "Aktueller Runtime-/Controllerstatus fehlt.";
    }
    if(!result.statusFresh && !result.operatorLocalFrame.isEmpty()) {
        for(const auto key:{"startReady","frameReady","resumeReady","localPositionKnown"})result.operatorLocalFrame[key]=false;
    }
    if(!m_localAdmissionError.isEmpty()) {
        result.operatorLocalFrame["reason"]=m_localAdmissionError;
        result.operatorLocalFrame["resultRevision"]=QVariant::fromValue(m_localAdmissionRevision);
        result.operatorLocalFrame["lastRequestAccepted"]=false;
        if(result.operatorLocalFrame.value("phase").toString()=="inactive")result.operatorLocalFrame["phase"]="invalid";
    }
    result.closing |= m_closing || m_mailbox->close.load();
    result.opening |= m_pendingOpen != 0;
    result.requestPending |= m_pendingOpen != 0 || !m_mailbox->normal.empty() ||
                             m_mailbox->dispatchInFlight || m_mailbox->continuityFence;
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
    if (!result.statusFresh && result.connected && !result.opening && !result.closing)
        result.state = "Runtime-/Controllerstatus veraltet";
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
bool send(const std::shared_ptr<ControllerRuntimeMailbox> &box, Request request) {
    request.issuedAt = nowMs();
    request.expiresAt = request.issuedAt + 250;
    return box->enqueue(std::move(request));
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
    send(m_mailbox, Request{Kind::Hold}); // An unsealed safe-hold request can never resume.
    emit changed();
}
bool ControllerRuntimeSession::overrides(int feed, int power) {
    Request r{Kind::Override};
    r.feed = feed;
    r.power = power;
    const bool accepted = send(m_mailbox, std::move(r));
    emit changed();
    return accepted; // Admission only; worker preflight still decides actual dispatch.
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
    QMutexLocker lock(&m_mailbox->mutex);
    stamp.proof = std::shared_ptr<const ControllerNativePressProof>(new ControllerNativePressProof(
        m_mailbox->inputIssuer, interaction, window, observedAt, stamp.expiresAtMs,
        m_mailbox->latest.connectionEpoch, m_mailbox->generation.load(),
        m_mailbox->focusGeneration.load()));
    return stamp;
}
ControllerRuntimeInputStamp ControllerRuntimeSession::sealNativeAction(ControllerNativeAction action,
        quint64 interaction,qint64 observedAt,qint64 expiresAt,const QByteArray &payload,
        std::shared_ptr<const NativeInputContextToken> context,quint64 connection,quint64 coordinate) {
    ControllerRuntimeInputStamp stamp;const auto now=nowMs();
    if(!interaction||observedAt<=0||observedAt>now||expiresAt<now||expiresAt-observedAt>250||
       !context||!context->valid||payload.size()!=32)return stamp;
    QMutexLocker lock(&m_mailbox->mutex);
    if(m_mailbox->latest.connectionEpoch!=connection||m_mailbox->latest.coordinateEpoch!=coordinate)return stamp;
    stamp.interactionId=interaction;stamp.observedAtMs=observedAt;stamp.expiresAtMs=expiresAt;
    stamp.windowEpoch=m_mailbox->focusWindowEpoch.load();
    stamp.proof=std::shared_ptr<const ControllerNativePressProof>(new ControllerNativePressProof(
        m_mailbox->inputIssuer,interaction,stamp.windowEpoch,observedAt,expiresAt,
        connection,m_mailbox->generation.load(),m_mailbox->focusGeneration.load(),
        action,payload,std::move(context),coordinate));
    return stamp;
}
bool ControllerRuntimeSession::submitNativeAction(ControllerNativeAction action,const ControllerRuntimeInputStamp &input,
        double x,double y,const QVariantMap &bounds,PreparedControllerJob::Ptr prepared,int feed,int power,PreparedLocalJob::Ptr local) {
    Kind kind=Kind::Start;
    switch(action){
    case ControllerNativeAction::FocusHold:kind=Kind::Focus;break;
    case ControllerNativeAction::StartJob:kind=Kind::Start;break;
    case ControllerNativeAction::Jog:kind=Kind::Jog;break;
    case ControllerNativeAction::Home:kind=Kind::Home;break;
    case ControllerNativeAction::Frame:kind=Kind::Frame;break;
    case ControllerNativeAction::WorkZero:kind=Kind::Zero;break;
    case ControllerNativeAction::Resume:kind=Kind::Resume;break;
    case ControllerNativeAction::Override:kind=Kind::Override;break;
    case ControllerNativeAction::LocalCapture:kind=Kind::LocalCapture;break;
    case ControllerNativeAction::LocalStart:kind=Kind::LocalStart;break;
    case ControllerNativeAction::LocalFrame:kind=Kind::LocalFrame;break;
    case ControllerNativeAction::LocalResume:kind=Kind::LocalResume;break;
    case ControllerNativeAction::SetupHome:kind=Kind::SetupHome;break;
    case ControllerNativeAction::SetupProbe:kind=Kind::SetupProbe;break;
    case ControllerNativeAction::SetupCorner:kind=Kind::SetupCorner;break;
    case ControllerNativeAction::SetupZero:kind=Kind::SetupZero;break;
    }
    Request request{kind};request.input=input;request.x=x;request.y=y;request.bounds=bounds;
    request.prepared=std::move(prepared);request.localPrepared=std::move(local);request.feed=feed;request.power=power;request.id=++m_nextRequest;
    const bool accepted=send(m_mailbox,std::move(request));emit changed();return accepted;
}
bool ControllerRuntimeSession::localData(const QVariantMap &data) {
    const auto id=++m_nextRequest;
    if(data.value("op")=="begin") {
        bool denied=false;
        {QMutexLocker lock(&m_mailbox->mutex);denied=m_mailbox->dispatchInFlight||!m_mailbox->normal.empty()||m_mailbox->latest.busy||m_mailbox->latest.firing||m_mailbox->latest.accessoryLeaseActive||m_mailbox->stop.load()||m_mailbox->close.load();}
        if(denied){m_localAdmissionError="Manueller Beginn abgelehnt: Geräteaktion aktiv oder ausstehend.";m_localAdmissionRevision=id;emit changed();return false;}
    }
    m_localAdmissionError.clear();
    if(data.value("op")=="begin"||data.value("op")=="cancel") {m_preparation->cancel();m_candidateMoves={};m_candidateLocalFrame.clear();}
    Request request{Kind::LocalData};request.bounds=data;request.id=id;
    const bool accepted=send(m_mailbox,std::move(request));
    if(!accepted){m_localAdmissionError="Lokale Anforderung nicht eingereiht.";m_localAdmissionRevision=id;}
    emit changed();return accepted;
}
bool ControllerRuntimeSession::operatorData(const QVariantMap &data) {
    Request request{Kind::OperatorData};request.bounds=data;request.id=++m_nextRequest;
    const bool accepted=send(m_mailbox,std::move(request));emit changed();return accepted;
}
bool ControllerRuntimeSession::commissioningData(const QVariantMap &data) {
    Request request{Kind::SetupData};request.bounds=data;request.id=++m_nextRequest;
    const bool accepted=send(m_mailbox,std::move(request));emit changed();return accepted;
}
void ControllerRuntimeSession::revokeFocusContext() {
    m_mailbox->focusWindowEpoch.fetch_add(1);
    endFocus();
}
bool ControllerRuntimeSession::jobPreparing() const {
    return m_preparation->preparing();
}
bool ControllerRuntimeSession::jobPrepared() const {
    const auto result = m_preparation->result(m_prepareSequence);
    return result && result->valid();
}
QString ControllerRuntimeSession::prepareReason() const {
    return m_preparation->reason();
}
void ControllerRuntimeSession::prepare(const JobPlan &plan) {
    const auto snapshot=status();const auto local=snapshot.operatorLocalFrame;
    const bool localMode=local.value("phase").toString()!="inactive" && !local.isEmpty();
    if(localMode && !local.value("localPositionKnown").toBool())return;
    const auto profile=plan.deviceProfile().outputFingerprint();
    const auto frame=localMode?local.value("frameId").toByteArray():QByteArray();
    if(plan.valid() && m_candidateMoves.constData()==plan.moves.constData() && m_candidateMoves.size()==plan.moves.size() &&
       m_candidateProfile==profile && m_candidateLocalFrame==frame && (jobPreparing()||jobPrepared())) {
        if(localMode && jobPrepared() && !local.value("prepared").toBool() && !snapshot.busy) {
            Request request{Kind::LocalData};request.bounds={{"op","candidate"},{"frameId",QString::fromLatin1(frame)}};request.localPrepared=m_preparation->localResult(m_prepareSequence);request.id=++m_nextRequest;send(m_mailbox,std::move(request));
        }
        return;
    }
    m_candidateMoves=plan.moves;m_candidateProfile=profile;m_candidateLocalFrame=frame;
    if(localMode) {
        localData({{"op","invalidateCandidate"}});
        ManualTravelCheck::Request request;
        request.headAnchor=QPointF(local.value("planHeadX").toDouble(),local.value("planHeadY").toDouble());
        const auto a=local.value("planAvailable").toMap();
        request.available=ManualTravelCheck::Distances{a.value("negativeX").toDouble(),a.value("positiveX").toDouble(),a.value("negativeY").toDouble(),a.value("positiveY").toDouble()};
        request.safetyMarginMm=2.;m_preparation->prepareLocal(plan,request,++m_prepareSequence);
    } else m_preparation->prepare(plan,++m_prepareSequence);
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
    requestStart(m_preparation->result(m_prepareSequence));
}

ControllerRuntimeWorker::ControllerRuntimeWorker(DeviceProfile profile,
                                                 std::shared_ptr<ControllerRuntimeMailbox> mailbox)
    : m_profile(std::move(profile)), m_mailbox(std::move(mailbox)) {
}
ControllerRuntimeWorker::~ControllerRuntimeWorker() = default;
std::optional<ControllerRuntimeWorker::ClockInterval> ControllerRuntimeWorker::readClockInterval() {
    timespec before{},boot{},after{};
    if (clock_gettime(CLOCK_MONOTONIC,&before)!=0 || clock_gettime(CLOCK_BOOTTIME,&boot)!=0 ||
        clock_gettime(CLOCK_MONOTONIC,&after)!=0) return {};
    const auto ns=[](const timespec &t) { return qint64(t.tv_sec)*1000000000LL+t.tv_nsec; };
    const auto first=ns(before),last=ns(after),value=ns(boot);
    // A delayed/descheduled sample is not permission to assume continuity.
    if (first<0 || last<first || value<0 || last-first>5000000) return {};
    return ClockInterval{value-last,value-first};
}
bool ControllerRuntimeWorker::checkClockContinuity() {
    if (m_continuityHandling) return false;
    const auto sample=m_clockSample();
    const bool wellFormed=sample && sample->lowNs>=-5000000 && sample->highNs>=0 && sample->highNs>=sample->lowNs &&
        quint64(sample->highNs)-quint64(sample->lowNs)<=5000000ULL;
    bool valid=wellFormed;
    if (valid && m_clockBaseline) {
        const auto low=std::max(sample->lowNs,m_clockBaseline->lowNs);
        const auto high=std::min(sample->highNs,m_clockBaseline->highNs);
        valid=low<=high;
        if (valid) m_clockBaseline=ClockInterval{low,high};
    } else if (valid) m_clockBaseline=sample;
    if (valid) return true;
    m_continuityHandling=true;
    const auto cleanup=qScopeGuard([this] { m_continuityHandling=false; });
    std::deque<Request> rejected;
    {
        QMutexLocker lock(&m_mailbox->mutex);
        m_mailbox->continuityFence=true;
        m_mailbox->generation.fetch_add(1);
        m_mailbox->stop.store(true);
        m_mailbox->normal.swap(rejected);
        m_mailbox->wakeLocked();
    }
    m_accessory.transition(AccessoryContract::Event::Fault);
    m_jobActive=m_focusActive=false;
    ++m_connectionEpoch;
    if (m_core) {
        m_core->fail("Host-Zeitkontinuität verloren. Verbindung gesperrt; vor neuer Ausgabe explizit neu verbinden und Referenz/Luftstrom erneut prüfen.",
                     ControllerDiagnostics::FaultCause::HostClockContinuity);
        retirePrepared();
    }
    // Reset the comparison baseline only after authority/queued work has been
    // revoked. This permits a later explicit Open; never automatic reconnection.
    m_clockBaseline=wellFormed ? sample : std::nullopt;
    publish();
    {
        QMutexLocker lock(&m_mailbox->mutex);
        m_mailbox->continuityFence=false;
    }
    for(const auto &request:rejected)
        if (request.kind==Kind::Open)
            emit openFinished(request.id,false,"Verbindung wegen verlorener Host-Zeitkontinuität abgelehnt.");
    retireControllerRequests(std::move(rejected));
    return false;
}
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
        return !checkClockContinuity() || m_mailbox->stop.load() || m_mailbox->close.load() || m_mailbox->shutdown.load();
    };
    m_core->dispatchPreflight = [this] { return cancelDispatch(); };
    m_core->dispatchAccepted = [this] { m_dispatchProof.reset(); };
    connect(m_core.get(), &ControllerSession::changed, this, &ControllerRuntimeWorker::coreChanged);
    m_watchdog->start();
    checkClockContinuity();
    publish();
}
void ControllerRuntimeWorker::drain() {
    if (!m_core)
        return;
    {
        QMutexLocker lock(&m_mailbox->mutex);
        m_mailbox->wakePending = false;
    }
    checkClockContinuity(); // Revocation/close latches below still run even after a clock fault.
    if (m_mailbox->focusOff.exchange(false) && (m_focusActive || m_core->firing))
        m_core->focus(false);
    if (m_mailbox->takeControl(m_mailbox->stop)) {
        if(m_stopHandoffProbe)m_stopHandoffProbe();
        invalidateLocalFrame("Lokaler Rahmen durch STOPP widerrufen.");
        cancelCommissioning("Einrichtung durch STOPP widerrufen.");
        discardOperatorWitness();m_activeOperatorOperation.clear();m_operatorPaused=false;
        m_accessory.transition(AccessoryContract::Event::Fault);
        m_jobActive = m_focusActive = false;
        if (!m_mailbox->close.load() && !m_core->awaitingReset)
            m_core->stop();
        retirePrepared();
    }
    if (m_mailbox->takeControl(m_mailbox->close)) {
        discardOperatorWitness();m_activeOperatorOperation.clear();m_operatorPaused=false;
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
            m_mailbox->dispatchInFlight = true;
            have = true;
        }
    }
    const auto retireRequest =
        qScopeGuard([&request] { retireControllerJob(std::move(request.prepared));retireLocalJob(std::move(request.localPrepared)); });
    const auto clearAdmission=qScopeGuard([this]{m_dispatchProof.reset();});
    if (have && request.generation == m_mailbox->generation.load() &&
        nowMs() <= request.expiresAt &&
        (request.kind == Kind::Open || request.connectionEpoch == m_connectionEpoch)) {
        if(request.kind!=Kind::Open&&request.kind!=Kind::Hold&&request.kind!=Kind::FocusOff&&
           request.kind!=Kind::Focus&&request.kind!=Kind::SetupData&&request.kind!=Kind::OperatorData&&request.kind!=Kind::LocalData&&!nativeActionAllowed(request)) {
            m_core->append("Native Geräteaktion abgelehnt: Eingabe, Argumente, Referenzrahmen oder Frist nicht bestätigt.");
        } else {
        m_dispatchProof=request.input.proof;
        if(m_local.engaged() && request.kind!=Kind::LocalData && request.kind!=Kind::LocalCapture && request.kind!=Kind::LocalStart && request.kind!=Kind::LocalFrame && request.kind!=Kind::LocalResume && request.kind!=Kind::OperatorData && request.kind!=Kind::Hold && request.kind!=Kind::FocusOff && request.kind!=Kind::Open) {
            m_core->append("Manueller lokaler Rahmen: globale Geräteaktion gesperrt.");
        } else if(m_commissioning.active() && m_commissioning.stage!="complete" &&
           request.kind!=Kind::SetupData && request.kind!=Kind::OperatorData && request.kind!=Kind::SetupHome && request.kind!=Kind::SetupProbe && request.kind!=Kind::SetupCorner && request.kind!=Kind::SetupZero && request.kind!=Kind::Open && request.kind!=Kind::Hold) {
            m_core->append("Einrichtung aktiv: normale Geräteaktion gesperrt.");
        } else switch (request.kind) {
        case Kind::Open: {
            invalidateLocalFrame("Neue Verbindung erfordert neuen lokalen Rahmen.");
            m_local={};
            ++m_connectionEpoch;
            discardOperatorWitness();m_operatorSession.clear();m_activeOperatorOperation.clear();m_operatorPaused=false;
            m_accessory = AccessoryContract(true);
            const bool opened = m_core->open(request.endpoint) && checkClockContinuity();
            emit openFinished(
                request.id, opened,
                opened ? QString() : QString("Controlleranschluss konnte nicht geöffnet werden."));
            break;
        }
        case Kind::LocalData:localData(request);break;
        case Kind::LocalCapture:case Kind::LocalStart:case Kind::LocalFrame:case Kind::LocalResume:localMotion(request);break;
        case Kind::OperatorData:operatorData(request.bounds);break;
        case Kind::SetupData:setupData(request.bounds);break;
        case Kind::SetupHome:case Kind::SetupProbe:case Kind::SetupCorner:case Kind::SetupZero:setupMotion(request);break;
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
        case Kind::Hold:
            if(!m_core->paused)m_core->pause(); // Never toggle a delayed hold into resume.
            if(m_core->paused && m_accessory.activeLease() && !m_operatorPaused) {
                m_operatorPaused=true;discardOperatorWitness();m_accessory.transition(AccessoryContract::Event::Pause);
            }
            break;
        case Kind::Resume:
            if(!m_core->paused||m_core->controllerState!="Hold"||m_core->firing||m_focusActive)break;
            if (!beginAccessory(AccessoryContract::OperatorAction::Resume))
                m_core->stop();
            else {m_operatorPaused=false;m_core->pause();}
            break;
        case Kind::Override:
            if (m_accessory.overridesError(m_accessoryClock.elapsed()).isEmpty())
                m_core->overrides(request.feed, request.power);
            break;
        case Kind::Focus: {
            refreshCoordinateEpoch();
            const auto &proof = request.input.proof;
            if (!proof || proof->issuer != m_mailbox->inputIssuer ||
                proof->window != m_mailbox->focusWindowEpoch.load() ||
                proof->connection != m_connectionEpoch ||
                proof->revocation != m_mailbox->generation.load() ||
                proof->focusGeneration != m_mailbox->focusGeneration.load() ||
                proof->action!=ControllerNativeAction::FocusHold ||
                (proof->contextToken&&!proof->contextToken->valid) ||
                (proof->contextToken&&(proof->coordinateEpoch!=m_coordinateEpoch||
                    proof->payloadDigest!=ControllerNativeDigest::request(ControllerNativeAction::FocusHold))) ||
                proof->interaction <= m_lastFocusInteraction || proof->observedAt > nowMs() ||
                proof->observedAt<=0 || proof->expiresAt < nowMs() || proof->expiresAt-proof->observedAt>250 ||
                request.focusGeneration != m_mailbox->focusGeneration.load() || !m_core->ready() ||
                !m_core->verified() || !m_core->positionReferenced ||
                !m_core->motionSafetyError().isEmpty())
                break;
            if (!beginAccessory(AccessoryContract::OperatorAction::Focus))
                break;
            m_lastFocusInteraction = proof->interaction;
            m_focusActive = true;
            m_core->focus(true);
            if (!m_core->firing) {
                m_focusActive = false;
                discardOperatorWitness();m_activeOperatorOperation.clear();m_operatorPaused=false;
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
            if (!beginAccessory(AccessoryContract::OperatorAction::StartJob,request.prepared?request.prepared->sourceFingerprint()+request.prepared->profileFingerprint():QByteArray()))
                break;
            m_jobActive = true;
            m_activePrepared = std::move(request.prepared);
            if (!m_core->startPrepared(*m_activePrepared)) {
                m_jobActive = false;
                discardOperatorWitness();m_activeOperatorOperation.clear();m_operatorPaused=false;
        m_accessory.transition(AccessoryContract::Event::Complete);
                retirePrepared();
            }
            break;
        }
        }
    } else if (have && request.kind == Kind::Open) {
        emit openFinished(request.id, false, "Verbindungsanforderung veraltet oder widerrufen.");
    }
    publish(true);
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
void ControllerRuntimeWorker::refreshCoordinateEpoch() {
    if(!m_core)return;
    const bool resetting=m_core->awaitingReset||m_core->handshaking;
    if(m_epochKnownOffset!=m_core->knownOffset||m_epochReferenced!=m_core->positionReferenced||
       m_epochHoming!=m_core->homingPending||m_epochResetting!=resetting||
       (m_core->knownOffset&&m_epochOffset!=m_core->offset))++m_coordinateEpoch;
    m_epochKnownOffset=m_core->knownOffset;m_epochReferenced=m_core->positionReferenced;
    m_epochHoming=m_core->homingPending;m_epochResetting=resetting;m_epochOffset=m_core->offset;
}
bool ControllerRuntimeWorker::nativeActionAllowed(const Request &request) {
    refreshCoordinateEpoch();ControllerNativeAction action=ControllerNativeAction::StartJob;
    double x=request.x,y=request.y;QByteArray source;
    switch(request.kind){
    case Kind::Start:action=ControllerNativeAction::StartJob;
        if(!request.prepared)return false;
        source=request.prepared->sourceFingerprint()+request.prepared->profileFingerprint();break;
    case Kind::LocalCapture:action=ControllerNativeAction::LocalCapture;break;
    case Kind::LocalResume:action=ControllerNativeAction::LocalResume;break;
    case Kind::LocalStart:case Kind::LocalFrame:
        action=request.kind==Kind::LocalStart?ControllerNativeAction::LocalStart:ControllerNativeAction::LocalFrame;
        if(!request.localPrepared)return false;
        source=request.localPrepared->fingerprint();if(request.kind==Kind::LocalFrame)source+=request.localPrepared->frameFingerprint();break;
    case Kind::SetupZero:action=ControllerNativeAction::SetupZero;break;
    case Kind::SetupHome:action=ControllerNativeAction::SetupHome;break;
    case Kind::SetupProbe:action=ControllerNativeAction::SetupProbe;break;
    case Kind::SetupCorner:action=ControllerNativeAction::SetupCorner;break;
    case Kind::Jog:action=ControllerNativeAction::Jog;break;
    case Kind::Home:action=ControllerNativeAction::Home;break;
    case Kind::Frame:action=ControllerNativeAction::Frame;break;
    case Kind::Zero:action=ControllerNativeAction::WorkZero;break;
    case Kind::Resume:action=ControllerNativeAction::Resume;break;
    case Kind::Override:action=ControllerNativeAction::Override;x=request.feed;y=request.power;break;
    default:return false;
    }
    const auto &proof=request.input.proof;const auto now=nowMs();
    if(!proof||proof->issuer!=m_mailbox->inputIssuer||proof->action!=action||
       proof->connection!=m_connectionEpoch||proof->revocation!=m_mailbox->generation.load()||
       proof->coordinateEpoch!=m_coordinateEpoch||proof->interaction<=m_lastNativeInteraction||
       proof->observedAt<=0||proof->observedAt>now||proof->expiresAt<now||proof->expiresAt-proof->observedAt>250||!proof->contextToken||!proof->contextToken->valid||
       proof->payloadDigest!=ControllerNativeDigest::request(action,x,y,request.bounds,source))return false;
    m_lastNativeInteraction=proof->interaction;return true;
}
bool ControllerRuntimeWorker::cancelDispatch() {
    if (!checkClockContinuity()) return true;
    if(m_dispatchProof && (m_dispatchProof->expiresAt<nowMs()||
       (m_dispatchProof->contextToken&&!m_dispatchProof->contextToken->valid)||
       (m_dispatchProof->action==ControllerNativeAction::FocusHold&&
        m_dispatchProof->focusGeneration!=m_mailbox->focusGeneration.load()))) {
        m_mailbox->revoke(false);
        return true;
    }
    if (m_mailbox->stop.load() || m_mailbox->close.load() || m_mailbox->shutdown.load())
        return true;
    if (!m_core || m_core->handshaking || m_core->awaitingReset || m_core->homingPending)
        return false;
    const auto status = m_core->status();
    const bool output = m_accessory.activeLease() || m_jobActive || m_focusActive || status.firing;
    if (status.coordinateUpdatePending && !output)
        return false;
    if(m_core->setupReadback.pending && !output)return false;
    if (!runtimeMotionError().isEmpty() ||
        (output && !m_accessory.preflightError(m_accessoryClock.elapsed()).isEmpty())) {
        m_mailbox->revoke(false);
        return true;
    }
    return false;
}
void ControllerRuntimeWorker::coreChanged() {
    if (!m_core)
        return;
    updateCommissioning();
    updateLocalFrame();
    const auto status = m_core->status();
    const bool output = m_accessory.activeLease() || m_jobActive || m_focusActive || status.firing;
    if(status.paused && m_accessory.activeLease() && !m_operatorPaused) {
        m_operatorPaused=true;discardOperatorWitness();m_accessory.transition(AccessoryContract::Event::Pause);
    }
    const bool lost =
        (m_wasVerified && !status.verified) || (m_wasReferenced && !status.positionReferenced);
    m_wasVerified = status.verified;
    m_wasReferenced = status.positionReferenced;
    if (!status.connected) {
        discardOperatorWitness();m_activeOperatorOperation.clear();m_operatorPaused=false;
        m_accessory.transition(AccessoryContract::Event::Disconnect);
        m_jobActive = m_focusActive = false;
        retirePrepared();
    } else if (lost && !(status.coordinateUpdatePending && !output) &&
               !(m_commissioning.stage=="readback" && m_core->setupReadback.pending && !output)) {
        discardOperatorWitness();m_activeOperatorOperation.clear();m_operatorPaused=false;
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
        discardOperatorWitness();m_activeOperatorOperation.clear();m_operatorPaused=false;
        m_accessory.transition(AccessoryContract::Event::Complete);
        retirePrepared();
    }
    publish();
}
void ControllerRuntimeWorker::watchdog() {
    if (!m_core)
        return;
    if (!checkClockContinuity()) return;
    updateCommissioning();
    updateLocalFrame();
    const auto status = m_core->status();
    const bool output = m_accessory.activeLease() || m_jobActive || m_focusActive || status.firing;
    const bool coordinate = status.coordinateUpdatePending && !output;
    const bool motion =
        status.executing && !status.referencing && !status.resetting && !coordinate &&
        !runtimeMotionError().isEmpty();
    const bool unsafeOutput =
        output &&
        (!runtimeMotionError().isEmpty() ||
         !m_accessory.preflightError(m_accessoryClock.elapsed()).isEmpty());
    if (motion || unsafeOutput) {
        discardOperatorWitness();m_activeOperatorOperation.clear();m_operatorPaused=false;
        m_accessory.transition(AccessoryContract::Event::Fault);
        m_jobActive = m_focusActive = false;
        m_core->stop();
    }
    if (!status.connected) {
        discardOperatorWitness();m_activeOperatorOperation.clear();m_operatorPaused=false;
        m_accessory.transition(AccessoryContract::Event::Disconnect);
        m_jobActive = m_focusActive = false;
    }
    publish();
}
void ControllerRuntimeWorker::publish(bool finishDispatch) {
    if (!m_core)
        return;
    refreshCoordinateEpoch();
    if(m_operatorWitness && (m_operatorWitnessCoordinate!=m_coordinateEpoch || m_accessoryClock.elapsed()>m_operatorWitness->expiresAtMs()))discardOperatorWitness();
    auto status = m_core->status();
    status.commissioning=commissioningSnapshot();
    status.operatorLocalFrame=localFrameSnapshot();
    status.doorSubstate=m_core->doorSubstate;
    status.extractionSupervision=extractionSupervisionSnapshot();
    const auto now = nowMs();
    status.connectionEpoch = m_connectionEpoch;
    status.coordinateEpoch=m_coordinateEpoch;
    status.holdConfirmed=m_core->paused&&m_core->controllerState=="Hold"&&!m_core->firing&&
        !m_core->homingPending&&!m_core->awaitingReset&&!m_core->handshaking;
    status.sequence = ++m_sequence;
    status.opening = m_core->awaitingReset || m_core->handshaking;
    status.accessoryLeaseActive = m_accessory.activeLease();
    status.accessoryJobActive = m_jobActive;
    status.accessoryFocusActive = m_focusActive;
    status.extractionSafetyError = m_accessory.preflightError(m_accessoryClock.elapsed());
    if(m_accessory.supervisionMode()!=AccessoryContract::SupervisionMode::SensorVerified && m_operatorPaused && !m_operatorWitness)
        status.extractionSafetyError=m_accessory.supervisionMode()==AccessoryContract::SupervisionMode::OperatorSupervisedDeviceCycle
            ?"Vor Fortsetzen eigene Anwesenheit erneut bestätigen; Lüfterzustände unbekannt."
            :"Vor Resume externe Absaugung und eigene Anwesenheit erneut bestätigen; Luftstrom nicht gemessen.";
    if(m_accessory.supervisionMode()!=AccessoryContract::SupervisionMode::SensorVerified) {
        status.overridesReady=false;status.overridesSafetyError=m_accessory.overridesError(m_accessoryClock.elapsed());
    }
    status.occupied = status.busy || status.firing || status.opening ||
                      status.accessoryLeaseActive || m_jobActive || m_focusActive;
    status.connectionPhase = !status.connected       ? (status.state == "Verbindungsfehler" ? QString("fault") : QString("closed"))
                             : m_core->handshaking   ? QString("handshake")
                             : m_core->awaitingReset ? QString("opening")
                             : status.occupied       ? QString("active")
                                                     : QString("idle");
    const auto diagnostic = m_core->diagnosticSnapshot();
    QMutexLocker lock(&m_mailbox->mutex);
    m_mailbox->latest = status;
    if (finishDispatch)
        m_mailbox->dispatchInFlight = false;
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
        publish(true); // Complete any control reservation with the actual closed snapshot.
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
