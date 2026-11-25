#include "machine.h"
#include "controllerruntime.h"
#include "controllerruntimemailbox.h"
#include "connectionhomepermit.h"
#include "nativeinputintent.h"
#include "document.h"
#include <QPointer>
#include <QCryptographicHash>
#include <QScopedValueRollback>
#include <QUuid>
#include <QJSValue>
#include <QDataStream>
#include <cmath>
namespace {
std::optional<ControllerNativeAction> nativeAction(const QString &name) {
    if(name=="connectHome")return ControllerNativeAction::ConnectHome;
    if(name=="localCapture")return ControllerNativeAction::LocalCapture;
    if(name=="localStart")return ControllerNativeAction::LocalStart;
    if(name=="localFrame")return ControllerNativeAction::LocalFrame;
    if(name=="localResume")return ControllerNativeAction::LocalResume;
    if(name=="setupZero")return ControllerNativeAction::SetupZero;
    if(name=="setupHome")return ControllerNativeAction::SetupHome;
    if(name=="setupProbe")return ControllerNativeAction::SetupProbe;
    if(name=="setupCorner")return ControllerNativeAction::SetupCorner;
    if(name=="start")return ControllerNativeAction::StartJob;
    if(name=="jog")return ControllerNativeAction::Jog;
    if(name=="home")return ControllerNativeAction::Home;
    if(name=="frame")return ControllerNativeAction::Frame;
    if(name=="zero")return ControllerNativeAction::WorkZero;
    if(name=="resume")return ControllerNativeAction::Resume;
    if(name=="override")return ControllerNativeAction::Override;
    if(name=="focus")return ControllerNativeAction::FocusHold;
    return {};
}
bool finiteInputNumber(const QVariant &value) {
    switch(value.metaType().id()) {
    case QMetaType::Double:case QMetaType::Float:case QMetaType::Int:case QMetaType::UInt:
    case QMetaType::LongLong:case QMetaType::ULongLong:return std::isfinite(value.toDouble());
    default:return false;
    }
}
}
Machine::Machine(Document *d, QObject *p, HardwareAccess access)
    : Simulator(p), document(d), hardwareAccess(access) {
    nativeInput = new NativeInputIntentAdapter(this,hardwareAccess==HardwareAccess::Allowed);
    connect(nativeInput,&NativeInputIntentAdapter::invalidated,this,[this] {
        if (auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get()))
            runtime->revokeFocusContext();
    });
    simulatorFrameNonce = QUuid::createUuid().toRfc4122();
    document->addDeviceProfileChangeGuard([self = QPointer<Machine>(this)] {
        return !self || (!self->occupied() && !self->requestPending() && !self->busy() && !self->firing() &&
                         (!self->session || !self->session->status().connected));
    });
    document->addSynchronousWorkGuard([self = QPointer<Machine>(this)]() -> QString {
        if (!self || self->simulation()) return {};
        if (self->occupied() || self->requestPending() || self->busy() || self->firing() || self->session->status().accessoryLeaseActive ||
            self->session->status().accessoryJobActive || self->session->status().accessoryFocusActive)
            return "Schwere Datei- und Geometrieaktionen warten, bis die aktive Geräteaktion beendet ist. STOPP bleibt verfügbar; die automatische Sicherung wird danach nachgeholt.";
        return {};
    });
    connect(this, &Simulator::changed, document, &Document::notifySynchronousWorkGuardChanged);
    connect(this, &QObject::destroyed, document, &Document::notifySynchronousWorkGuardChanged);
    document->notifySynchronousWorkGuardChanged();
    syncDeviceProfile();
    connect(d, &Document::deviceProfileChanged, this, &Machine::syncDeviceProfile);
    connect(this, &Simulator::changed, this, &Machine::refreshPreviewPosition);
    connect(d, &Document::changed, this, &Machine::refreshPreviewPosition);
    connect(d, &Document::changed, this, [this] {
        ++nativeDocumentGeneration;
        if(live && operatorLocalFrame().value("phase").toString()!="inactive" && !operatorLocalFrame().isEmpty()) {
            const QPointer<Machine> alive(this);nativeInput->revoke();if(!alive)return;
        }
        if (live && !document->planningBusy())
            if (auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get())) runtime->prepare(document->job());
    });
    connect(d,&Document::selectionChanged,this,[this]{++nativeDocumentGeneration;});
    refreshPreviewPosition();
}
void Machine::setPreviewDeviceBinding(const QByteArray &binding) {
    if (previewDeviceBinding == binding) return;
    previewDeviceBinding = binding; // Publish before revocation notifications can reenter.
    const QPointer<Machine> alive(this);
    nativeInput->revoke();
    if (!alive) return;
    if (live && (connected() || occupied() || requestPending())) {
        document->notify("Gerätebindung geändert. Verbindung vor weiterer Bearbeitung erneut prüfen.");
        session->close();
    }
    refreshPreviewPosition();
}
void Machine::refreshPreviewPosition() {
    if (updatingPreview) return;
    QScopedValueRollback<bool> guard(updatingPreview, true);
    if (live) {
        const auto local=operatorLocalFrame();
        if(!local.value("localPositionKnown").toBool()) {document->clearPreviewPosition();return;}
        if(busy()||firing())return;
        const auto profile=document->deviceProfile().outputFingerprint();
        const auto frame=QCryptographicHash::hash(QByteArray("operator-local-preview:")+local.value("frameId").toByteArray()+profile+previewDeviceBinding,QCryptographicHash::Sha256).toHex();
        const QPointF point(local.value("planHeadX").toDouble(),local.value("planHeadY").toDouble());
        if(!previewPositionRevision||previewFrame!=frame||previewPoint!=point){++previewPositionRevision;previewFrame=frame;previewPoint=point;}
        document->setPreviewPosition({point,true,profile,frame,previewPositionRevision});return;
    }
    // Freeze the job's start snapshot during simulation or a focus indication.
    if (busy() || firing()) return;
    if (!Simulator::ready()) { document->clearPreviewPosition(); return; }
    const auto profile = document->deviceProfile().outputFingerprint();
    const auto frame = QCryptographicHash::hash(
        QByteArray("blazer-simulator-frame-v1:") + simulatorFrameNonce + profile + previewDeviceBinding,
        QCryptographicHash::Sha256).toHex();
    const QPointF point(Simulator::x(),Simulator::y());
    if (previewPositionRevision == 0 || frame != previewFrame ||
        point.x() != previewPoint.x() || point.y() != previewPoint.y()) {
        ++previewPositionRevision;
        previewFrame = frame; previewPoint = point;
    }
    document->setPreviewPosition({point,true,profile,frame,previewPositionRevision});
}
void Machine::syncDeviceProfile() {
    if (hardwareAccess == HardwareAccess::Disabled) {
        // Offline export/self-test never needs a transport owner or its thread.
        live = false;
        pendingSimulator = false;
        session.reset();
        const QPointer<Machine> alive(this);
        Simulator::setDeviceProfile(document->deviceProfile());
        if (alive) emit changed();
        return;
    }
    if (session && session->deviceProfile().outputFingerprint() ==
                       document->deviceProfile().outputFingerprint())
        return;
    const QPointer<Machine> alive(this);
    nativeInput->revoke();
    if (!alive) return;
    live = false;
    QString error;
    session = ControllerSessions::create(document->deviceProfile(), error);
    if (session)
        connect(session.get(), &ControllerSession::changed, this, [this] {
            const QPointer<Machine> alive(this);
            const auto status=session->status();
            const bool revokeInput=status.connectionEpoch!=nativeConnectionEpoch ||
                                   (nativeSessionVerified && !status.verified);
            nativeConnectionEpoch=status.connectionEpoch;
            nativeSessionVerified=status.verified;
            if (revokeInput) {
                nativeInput->revoke();
                if (!alive) return;
            }
            if (pendingSimulator && !session->status().connected && !session->status().occupied) {
                pendingSimulator = false;
                live = false;
                simulatorFrameNonce = QUuid::createUuid().toRfc4122();
                Simulator::stop();
                if (!alive) return;
                refreshPreviewPosition();
                if (!alive) return;
            }
            emit changed();
        });
    else
        document->notify(error);
    if (session)
        connect(session.get(), &ControllerSession::connectionRequestFinished,
                this, &Machine::connectionRequestFinished);
    Simulator::setDeviceProfile(document->deviceProfile());
    emit changed();
}
QStringList Machine::ports() const {
    if (hardwareAccess == HardwareAccess::Disabled)
        return {};
    return ControllerSessions::availableEndpoints(document->deviceProfile());
}
bool Machine::connectHardware(const QString &port) {
    return requestConnectHardware(port, {}).enqueued;
}
ControllerSubmission Machine::requestConnectHardware(const QString &port, const QByteArray &binding, bool autoHome) {
    if (hardwareAccess == HardwareAccess::Disabled)
        return {0,false,"Hardwareanschluss in diesem Offline-Lauf deaktiviert."};
    const auto error = DeviceBackends::runtimeSupportError(document->deviceProfile());
    if (!error.isEmpty()) return {0,false,error};
    if (!session || occupied() || session->status().connected)
        return {0,false,"Verbindung oder Geräteaktion noch belegt."};
    const QPointer<Machine> alive(this);
    std::optional<NativeInputIntentAdapter::Intent> connectionInput;
    QByteArray connectionContext;
    if(autoHome) {
        connectionContext=nativeBinding("connectHome",{{"endpoint",port},{"binding",QString::fromLatin1(binding)}},session->status());
        if(!connectionContext.isEmpty())connectionInput=nativeInput->consumeMatching("connectHome",connectionContext);
        if(!alive)return {0,false,"Verbindungsbesitzer beendet."};
        if(!connectionInput)return {0,false,"Verbinden mit Referenzfahrt benötigt einen frischen bewussten Klick auf die passende Verbindungstaste. "+nativeInput->reason()};
    } else nativeInput->revoke();
    if (!alive) return {0,false,"Verbindungsbesitzer beendet."};
    Simulator::stop();
    if (!alive) return {0,false,"Verbindungsbesitzer beendet."};
    live = true; pendingSimulator = false;
    document->clearPreviewPosition();
    if (!alive) return {0,false,"Verbindungsbesitzer beendet."};
    ControllerSubmission result;
    if(autoHome) {
        auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get());
        if(!runtime || nativeBinding("connectHome",{{"endpoint",port},{"binding",QString::fromLatin1(binding)}},session->status())!=connectionContext)
            return {0,false,"Gerätezuordnung vor der Verbindung verändert; erneut ausdrücklich verbinden."};
        result=runtime->requestOpenAndHome(port,binding,connectionInput->interaction,connectionInput->observedAtMs,connectionInput->expiresAtMs,connectionContext);
        if(!alive)return result;
        // The accepted connection operation now owns its bounded Home intent.
        // Closing the popup/revoking the consumed click must not mint a new one.
        nativeInput->revoke();
    } else result=session->requestOpen(port,binding);
    if (!alive) return result;
    if (auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get()); runtime && !document->planningBusy()) runtime->prepare(document->job());
    if (!alive) return result;
    if (!result.enqueued) document->notify(result.reason);
    if (!alive) return result;
    emit changed(); return result;
}
bool Machine::occupied() const { return pendingSimulator || busy() || firing() || (live && session && session->status().occupied); }
bool Machine::requestPending() const { return pendingSimulator || (live && session && session->status().requestPending); }
bool Machine::statusFresh() const { return live && session && session->status().statusFresh; }
qint64 Machine::statusAgeMs() const { return live && session ? session->status().statusAgeMs : -1; }
QString Machine::connectionPhase() const { return pendingSimulator?QString("closing"):live && session ? session->status().connectionPhase : "closed"; }
bool Machine::jobPreparing() const { const auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get()); return live && runtime && runtime->jobPreparing(); }
bool Machine::jobPrepared() const { const auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get()); return !live || (runtime&&runtime->jobPrepared()); }
QString Machine::prepareReason() const { const auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get()); return live&&runtime?runtime->prepareReason():QString(); }

QString Machine::nativeInputSafetyError() const { return live ? nativeInput->reason() : QString(); }
void Machine::registerFocusControl(QObject *control) {
    focusControl = control;
    nativeInput->registerControl(control,"focus",[self=QPointer<Machine>(this)] {
        return self&&self->session?self->nativeBinding("focus",{},self->session->status()):QByteArray();
    });
}
QVariantMap Machine::nativeArguments(QObject *control,const QVariantMap &fixed) const {
    if(!fixed.isEmpty()||!control)return fixed;
    auto value=control->property("nativeActionArguments");
    if(value.metaType().id()==qMetaTypeId<QJSValue>())value=value.value<QJSValue>().toVariant();
    if(!value.isValid())return {};
    if(value.metaType().id()!=QMetaType::QVariantMap)return {{"invalid",true}};
    const auto map=value.toMap();return map.size()<=2?map:QVariantMap{{"invalid",true}};
}
void Machine::registerNativeActionControl(QObject *control,const QString &action,const QVariantMap &fixedArguments) {
    if(!control||!nativeAction(action)||action=="focus"||fixedArguments.size()>2)return;
    const auto key=action+"#"+QString::number(quintptr(control),16);
    nativeInput->registerClickControl(control,key,[self=QPointer<Machine>(this),target=QPointer<QObject>(control),action,fixedArguments] {
        if(!self||!target||!self->session)return QByteArray();
        return self->nativeBinding(action,self->nativeArguments(target,fixedArguments),self->session->status());
    });
}
QByteArray Machine::nativeBinding(const QString &action,const QVariantMap &arguments,const ControllerStatus &status) const {
    if(action=="connectHome") {
        if(hardwareAccess!=HardwareAccess::Allowed || !session || pendingSimulator || status.connected || status.busy || status.occupied || status.opening || status.closing || status.requestPending || status.resetting ||
           arguments.size()!=2 || arguments.value("endpoint").metaType().id()!=QMetaType::QString || arguments.value("binding").metaType().id()!=QMetaType::QString)return {};
        const auto endpoint=arguments.value("endpoint").toString();const auto binding=arguments.value("binding").toString().toLatin1();
        if(endpoint.isEmpty()||endpoint.size()>4096||endpoint.contains(QChar(0))||binding.size()!=64||binding!=previewDeviceBinding)return {};
        for(const auto c:binding)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return {};
        return ControllerConnectionHomeDigest::context(endpoint,binding,session->deviceProfile().outputFingerprint());
    }
    const auto kind=nativeAction(action);if(!kind||!live||!session||!status.connected||!status.statusFresh||
        status.opening||status.closing||status.requestPending||status.referencing||status.resetting||status.coordinateUpdatePending)return {};
    if(action.startsWith("local")) {
        if(!arguments.isEmpty())return {};
        const auto f=status.operatorLocalFrame;const auto phase=f.value("phase").toString();
        if(action=="localCapture") {if(phase!="captureReady"||status.busy||status.firing||status.accessoryLeaseActive)return {};}
        else if(action=="localResume") {if(phase!="paused"||!f.value("resumeReady").toBool())return {};}
        else if(action=="localFrame") {if(!f.value("frameReady").toBool())return {};}
        else if(action=="localStart") {if(!f.value("startReady").toBool())return {};}
        else return {};
        if((action=="localStart"||action=="localFrame") && (document->startMode()!=Document::CurrentPosition||document->manualRegistrationEnabled()||document->planningBusy()))return {};
        QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);
        stream<<action<<f.value("frameId").toString()<<f.value("approvalId").toString()<<nativeDocumentGeneration<<status.connectionEpoch<<status.coordinateEpoch<<previewDeviceBinding;
        return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
    }
    if(!status.operatorLocalFrame.isEmpty() && status.operatorLocalFrame.value("phase").toString()!="inactive")return {};
    if(action.startsWith("setup")) {
        const auto setup=status.commissioning;const auto stage=setup.value("stage").toString();
        if(status.busy||status.firing||status.accessoryLeaseActive||status.accessoryJobActive||status.accessoryFocusActive)return {};
        if(action=="setupZero") {if(stage!="workCoordinates"||!arguments.isEmpty())return {};}
        else if(action=="setupHome") {if((stage!="homeReady"&&stage!="savedHomeReady")||!arguments.isEmpty())return {};}
        else if(action=="setupProbe") {if(stage!="geometry"||!setup.value("homeObserved").toBool()||arguments.size()!=2||!finiteInputNumber(arguments.value("dx"))||!finiteInputNumber(arguments.value("dy")))return {};}
        else if(action=="setupCorner") {if((stage!="cornerReady"&&stage!="savedApproachReady")||arguments.size()!=1||arguments.value("index").toInt()!=setup.value("cornerIndex").toInt())return {};}
        else return {};
        QByteArray data;QDataStream stream(&data,QIODevice::WriteOnly);
        stream<<action<<QJsonDocument::fromVariant(arguments).toJson(QJsonDocument::Compact)<<setup.value("fingerprint").toString()<<stage<<status.connectionEpoch<<status.coordinateEpoch;
        return QCryptographicHash::hash(data,QCryptographicHash::Sha256);
    }
    if(status.commissioning.value("active").toBool() && status.commissioning.value("stage").toString()!="complete")return {};
    if(action=="jog") {
        if(arguments.size()!=2||!finiteInputNumber(arguments.value("dx"))||!finiteInputNumber(arguments.value("dy")))return {};
    } else if(action=="frame") {
        if(arguments.size()!=1||!QStringList{"job","selection","tools","t1","t2"}.contains(arguments.value("scope").toString()))return {};
    } else if(action=="override") {
        const auto f=arguments.value("feed"),p=arguments.value("power");
        if(arguments.size()!=2||!finiteInputNumber(f)||!finiteInputNumber(p)||std::trunc(f.toDouble())!=f.toDouble()||
            std::trunc(p.toDouble())!=p.toDouble()||f.toDouble()<10||f.toDouble()>200||p.toDouble()<10||p.toDouble()>100||!overridesReady())return {};
    } else if(!arguments.isEmpty())return {};
    if(action=="home") {if(status.busy||!status.homingSafetyError.isEmpty())return {};}
    else if(action=="resume") {if(!status.paused||!status.holdConfirmed||status.firing||status.accessoryFocusActive||!processingSafetyErrors().isEmpty())return {};}
    else if(action!="override") {
        if(!status.ready||status.busy||status.firing||!status.verified||!status.positionReferenced||!status.motionSafetyError.isEmpty())return {};
        if((action=="start"||action=="focus")&&!processingSafetyErrors().isEmpty())return {};
    }
    if(QStringList{"operatorAttendedExternal","operatorSupervisedDeviceCycle"}.contains(status.extractionSupervision.value("mode").toString()) &&
       (action=="start" || action=="focus" || action=="resume") &&
       status.extractionSupervision.value("pendingAction").toString()!=action)return {};
    const auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get());
    PreparedControllerJob::Ptr prepared;
    if(action=="start") {
        if(!runtime||document->planningBusy()||document->startMode()!=Document::AbsoluteCoordinates)return {};
        prepared=runtime->m_preparation->result(runtime->m_prepareSequence);
        if(!prepared||!prepared->valid())return {};
    }
    QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_6_8);
    stream<<action<<QJsonDocument::fromVariant(arguments).toJson(QJsonDocument::Compact)<<nativeDocumentGeneration
        <<status.connectionEpoch<<status.coordinateEpoch<<session->deviceProfile().outputFingerprint()<<previewDeviceBinding;
    if(prepared)stream<<prepared->sourceFingerprint()<<runtime->m_prepareSequence;
    return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
}
bool Machine::dispatchNative(const QString &action,const QVariantMap &arguments,double x,double y,
        const QVariantMap &bounds,PreparedControllerJob::Ptr prepared,int feed,int power) {
    const auto kind=nativeAction(action);auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get());
    if(!runtime||!kind)return false;
    const auto snapshot=runtime->status();
    const auto binding=nativeBinding(action,arguments,snapshot);
    const QPointer<Machine> alive(this);
    const auto denied=[this,alive,action](const QString &reason) {
        document->notify(reason);
        if (alive) emit nativeActionDenied(action,reason);
        return false;
    };
    const auto input=binding.isEmpty()?std::nullopt:nativeInput->consumeMatching(action,binding);
    if(!alive)return false;
    if(!input) {
        const auto platformReason=nativeInput->reason();
        return denied(!platformReason.isEmpty() ? platformReason : binding.isEmpty()
            ? QString("Geräteaktion gesperrt: aktueller Controller- oder Einrichtungskontext gibt diese Aktion nicht frei.")
            : QString("Geräteaktion gesperrt: frischer nativer Druck und zugehöriges Loslassen am passenden Bedienelement fehlen."));
    }
    QByteArray source;if(prepared)source=prepared->sourceFingerprint()+prepared->profileFingerprint();
    const auto digest=ControllerNativeDigest::request(*kind,action=="override"?double(feed):x,action=="override"?double(power):y,bounds,source);
    const auto stamp=runtime->sealNativeAction(*kind,input->interaction,input->observedAtMs,input->expiresAtMs,digest,
        input->contextToken,snapshot.connectionEpoch,snapshot.coordinateEpoch);
    if(!stamp.proof)return denied("Geräteaktion gesperrt: Eingabe oder Referenzrahmen inzwischen verändert.");
    const bool accepted=runtime->submitNativeAction(*kind,stamp,x,y,bounds,std::move(prepared),feed,power);
    if (!alive) return accepted;
    if (!accepted) return denied("Geräteaktion nicht angenommen: aktuelle Runtime-Freigabe fehlt. Es wurde keine Bewegung freigegeben.");
    return true;
}
void Machine::beginManualPlacement(){
    if(auto r=dynamic_cast<ControllerRuntimeSession *>(session.get());live&&r){
        const QPointer<Machine> alive(this);nativeInput->revoke();if(!alive)return;
        const bool accepted=r->localData({{"op","begin"}});if(!alive)return;
        if(!accepted)document->notify("Manuelle Vorbereitung abgelehnt: aktive oder ausstehende Geräteaktion zuerst beenden.");
    }
}
void Machine::setManualTravel(double nx,double px,double ny,double py){if(auto r=dynamic_cast<ControllerRuntimeSession *>(session.get());live&&r)r->localData({{"op","travel"},{"negativeX",nx},{"positiveX",px},{"negativeY",ny},{"positiveY",py}});}
void Machine::attestManualPlacement(bool motors,bool measured,bool stationary,bool closed){if(auto r=dynamic_cast<ControllerRuntimeSession *>(session.get());live&&r)r->localData({{"op","attest"},{"frameId",operatorLocalFrame().value("frameId")},{"motorsFree",motors},{"measuredClear",measured},{"headStationary",stationary},{"lidClosed",closed}});}
void Machine::captureOperatorLocalFrame(){dispatchLocalNative("localCapture");}
void Machine::prepareLocalJob(){
    if(!live||!session)return;
    auto r=dynamic_cast<ControllerRuntimeSession *>(session.get());
    if(!r)return;
    if(document->startMode()!=Document::CurrentPosition||document->manualRegistrationEnabled()){const QPointer<Machine> alive(this);r->localData({{"op","invalidateCandidate"}});if(alive)document->notify("Lokaler manueller Start benötigt Aktuelle Position ohne Print-and-Cut-Registrierung.");return;}
    if(!document->planningBusy())r->prepare(document->job());
}
void Machine::approveLocalTravel(const QString &id){if(auto r=dynamic_cast<ControllerRuntimeSession *>(session.get());live&&r)r->localData({{"op","approve"},{"approvalId",id}});}
void Machine::startLocalJob(){dispatchLocalNative("localStart");}
void Machine::frameLocalJob(){dispatchLocalNative("localFrame");}
void Machine::resumeLocalJob(){dispatchLocalNative("localResume");}
void Machine::cancelOperatorLocalFrame(){if(auto r=dynamic_cast<ControllerRuntimeSession *>(session.get());live&&r){const QPointer<Machine> alive(this);if(busy()||firing())r->stop();if(!alive)return;r->localData({{"op","cancel"}});}}
bool Machine::dispatchLocalNative(const QString &action){
    auto r=dynamic_cast<ControllerRuntimeSession *>(session.get());const auto kind=nativeAction(action);if(!live||!r||!kind)return false;
    const auto status=r->status();const auto binding=nativeBinding(action,{},status);const QPointer<Machine> alive(this);
    const auto input=binding.isEmpty()?std::nullopt:nativeInput->consumeMatching(action,binding);if(!alive)return false;
    if(!input){document->notify("Lokale Aktion gesperrt: aktuelle Vorbereitung und frischer nativer Druck mit Loslassen fehlen.");return false;}
    PreparedLocalJob::Ptr p;if(action=="localStart"||action=="localFrame")p=r->m_preparation->localResult(r->m_prepareSequence);
    if((action=="localStart"||action=="localFrame")&&(!p||!p->valid()))return false;
    const QVariantMap bounds{{"frameId",status.operatorLocalFrame.value("frameId")},{"approvalId",status.operatorLocalFrame.value("approvalId")}};
    auto source=p?p->fingerprint():QByteArray();if(p&&action=="localFrame")source+=p->frameFingerprint();
    const auto digest=ControllerNativeDigest::request(*kind,0,0,bounds,source);
    const auto proof=r->sealNativeAction(*kind,input->interaction,input->observedAtMs,input->expiresAtMs,digest,input->contextToken,status.connectionEpoch,status.coordinateEpoch);
    if(!proof.proof)return false;
    return r->submitNativeAction(*kind,proof,0,0,bounds,{},100,100,std::move(p));
}
void Machine::selectSupervisedDeviceCycle(){
    if(auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get());live&&runtime)runtime->operatorData({{"op","selectDeviceCycle"}});
}
void Machine::attestDeviceCycle(const QString &action,bool operatorPresent){
    auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get());if(!live||!runtime)return;
    QByteArray source;
    if(action=="start") {
        const auto prepared=runtime->m_preparation->result(runtime->m_prepareSequence);
        if(!prepared||!prepared->valid()){document->notify("Anwesenheitsbestätigung: aktuellen Auftrag zuerst fertig berechnen lassen.");return;}
        source=prepared->sourceFingerprint()+prepared->profileFingerprint();
    }
    runtime->operatorData({{"op","attestDeviceCycle"},{"action",action},{"source",source},{"operatorPresent",operatorPresent}});
}
void Machine::revokeDeviceAttendance(){
    if(auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get());live&&runtime) {
        runtime->stop();runtime->operatorData({{"op","withdraw"}});
    }
}
void Machine::selectAttendedExtraction(){
    if(auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get());live&&runtime)runtime->operatorData({{"op","select"}});
}
void Machine::attestExternalExtraction(const QString &action,bool extractionOn,bool operatorPresent){
    auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get());if(!live||!runtime)return;
    QByteArray source;
    if(action=="start") {
        const auto prepared=runtime->m_preparation->result(runtime->m_prepareSequence);
        if(!prepared||!prepared->valid()){document->notify("Absaugungsbestätigung: aktuellen Auftrag zuerst fertig berechnen lassen.");return;}
        source=prepared->sourceFingerprint()+prepared->profileFingerprint();
    }
    runtime->operatorData({{"op","attest"},{"action",action},{"source",source},{"extractionOn",extractionOn},{"operatorPresent",operatorPresent}});
}
void Machine::revokeExternalAttendance(){
    if(auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get());live&&runtime) {
        runtime->stop();runtime->operatorData({{"op","withdraw"}});
    }
}
void Machine::confirmExtractionClearance(bool confirmed){
    if(auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get());live&&runtime)runtime->operatorData({{"op","clearance"},{"confirmed",confirmed}});
}
void Machine::commissioningData(const QVariantMap &data) {
    if(auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get());live&&runtime)
        runtime->commissioningData(data);
}
void Machine::beginCommissioning(){commissioningData({{"op","begin"}});}
void Machine::attestCommissioningMechanics(bool switches,bool direction,bool freeTravel,bool laserDisabled) {
    commissioningData({{"op","mechanics"},{"switches",switches},{"direction",direction},{"freeTravel",freeTravel},{"laserDisabled",laserDisabled}});
}
void Machine::commissioningHome(){dispatchNative("setupHome",{});}
void Machine::confirmCommissioningHome(){commissioningData({{"op","homeObserved"}});}
void Machine::commissioningProbe(double dx,double dy){dispatchNative("setupProbe",{{"dx",dx},{"dy",dy}},dx,dy);}
void Machine::confirmCommissioningProbe(){commissioningData({{"op","probeObserved"}});}
void Machine::setCommissioningLimits(double minX,double minY,double maxX,double maxY){commissioningData({{"op","limits"},{"minX",minX},{"minY",minY},{"maxX",maxX},{"maxY",maxY}});}
void Machine::commissioningCorner(int index){dispatchNative("setupCorner",{{"index",index}},double(index));}
void Machine::confirmCommissioningCorner(){commissioningData({{"op","cornerObserved"}});}
void Machine::commissioningZero(){dispatchNative("setupZero",{});}
void Machine::confirmCommissioningCoordinates(){commissioningData({{"op","coordinatesObserved"}});}
void Machine::cancelCommissioning(){if(auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get());live&&runtime){runtime->stop();runtime->commissioningData({{"op","cancel"}});}}
void Machine::beginFocus() {
    if (!live) { Simulator::focus(true); return; }
    auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get());
    const auto snapshot=runtime?runtime->status():ControllerStatus{};
    const QPointer<Machine> alive(this);
    const auto input=nativeInput->consume("focus");
    if(!alive)return;
    if (!runtime || !input || input->binding!=nativeBinding("focus",{},snapshot)) {
        const auto reason=nativeInput->reason();
        document->notify(reason.isEmpty() ? QString("Fokus gesperrt: frischer nativer Druck auf das Fokusfeld fehlt.") : reason);
        return;
    }
    const auto stamp=runtime->sealNativeAction(ControllerNativeAction::FocusHold,input->interaction,
        input->observedAtMs,input->expiresAtMs,ControllerNativeDigest::request(ControllerNativeAction::FocusHold),
        input->contextToken,snapshot.connectionEpoch,snapshot.coordinateEpoch);
    if(!stamp.proof){document->notify("Fokus gesperrt: Eingabe oder Referenzrahmen inzwischen verändert.");return;}
    runtime->beginFocus(stamp);
}
void Machine::endFocus() { focus(false); }
void Machine::useSimulator() {
    const QPointer<Machine> alive(this);
    nativeInput->revoke();
    if (!alive) return;
    if (live && session && (session->status().connected || session->status().occupied)) {
        pendingSimulator = true; session->close(); if (!alive) return; emit changed(); return;
    }
    pendingSimulator = false; live = false;
    simulatorFrameNonce = QUuid::createUuid().toRfc4122();
    Simulator::stop(); if (!alive) return; refreshPreviewPosition(); if (!alive) return; emit changed();
}
void Machine::toggleConnection() {
    if (live)
        disconnectFault();
    else
        Simulator::toggleConnection();
}
void Machine::jog(double x, double y) {
    if (live)
        dispatchNative("jog",{{"dx",x},{"dy",y}},x,y);
    else
        Simulator::jog(x, y);
}
void Machine::setWorkZero() {
    if (!live || !session) return;
    if (busy() || firing() || session->status().accessoryLeaseActive || session->status().accessoryJobActive || session->status().accessoryFocusActive) {
        document->notify("Arbeitsnullpunkt gesperrt: erst den aktiven Auftrag, Fokus oder die laufende Geräteaktion beenden.");
        return;
    }
    dispatchNative("zero",{});
}
void Machine::home() {
    if (live)
        dispatchNative("home",{});
    else
        Simulator::home();
}
void Machine::focus(bool on) {
    if (!live) { Simulator::focus(on); return; }
    if (!on) nativeInput->revokeFocus(); // Focus-only; unrelated queued job authority is preserved.
    else beginFocus(); // Legacy true has no input authority and cannot bypass the native gate.
}
void Machine::frame(const QVariantMap &b) {
    if (b.value("outputFingerprint").toString() !=
        QString::fromLatin1(document->deviceProfile().outputFingerprint())) {
        document->notify("Rahmenziel gehört zu einem anderen Geräteprofil.");
        return;
    }
    if (live && document->startMode() != Document::AbsoluteCoordinates) {
        document->notify("Relative Rahmenfahrt ist für reale Hardware noch nicht eingerichtet.");
        return;
    }
    const auto current = document->framingBounds(b.value("scope").toString());
    if (!b.value("valid").toBool() || !current.value("valid").toBool() || b != current) {
        document->notify("Rahmenziel ist veraltet oder verändert. Aktuelle Vorschau erneut prüfen.");
        return;
    }
    if (live)
        dispatchNative("frame",{{"scope",current.value("scope")}},0,0,current);
    else
        Simulator::frame(current);
}
void Machine::start() {
    if (document->planningBusy()) {
        document->notify("Auftragsberechnung oder Export läuft noch.");
        return;
    }
    if (busy() || firing())
        return;
    if (live && document->startMode() != Document::AbsoluteCoordinates) {
        document->notify("Relative Startmodi sind derzeit nur im Simulator eingerichtet. Reale Positions- und Koordinatenbindung ist noch nicht abgenommen.");
        return;
    }
    if (live && !processingSafetyErrors().isEmpty()) {
        document->notify(processingSafetyErrors().join("\n"));
        return;
    }
    const auto &job = document->job();
    if (job.deviceProfile().outputFingerprint() != document->deviceProfile().outputFingerprint()) {
        document->notify("Geräteprofil des Auftrags ist veraltet; bitte neu berechnen.");
        return;
    }
    if (!job.valid()) {
        document->notify(job.errors.join("; "));
        return;
    }
    if (live) {
        auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get());if(!runtime)return;
        if(runtime->m_candidateMoves.constData()!=job.moves.constData()||runtime->m_candidateMoves.size()!=job.moves.size()||!runtime->jobPrepared()) {
            runtime->prepare(job);return; // No completion callback can start this job.
        }
        dispatchNative("start",{},0,0,{},runtime->m_preparation->result(runtime->m_prepareSequence));
    } else {
        Simulator::setJob(job);
        Simulator::start();
    }
}
void Machine::pause() {
    if (live) {
        if (paused()) {
            if(!processingSafetyErrors().isEmpty()) {
                document->notify(processingSafetyErrors().join("\n"));stop();return;
            }
            dispatchNative("resume",{});
        } else {
            nativeInput->discardMatching("resume");
            session->pause(); // A hold request can never turn into resume in the worker.
        }
    } else
        Simulator::pause();
}
bool Machine::setOverrides(int feed,int power) {
    if(!overridesReady())return false;
    return dispatchNative("override",{{"feed",feed},{"power",power}},0,0,{}, {},feed,power);
}
void Machine::stop() {
    const QPointer<Machine> alive(this);
    nativeInput->revoke();
    if (!alive) return;
    if (live)
        session->stop();
    else
        Simulator::stop();
}
void Machine::disconnectFault() {
    const QPointer<Machine> alive(this);
    nativeInput->revoke();
    if (!alive) return;
    if (live)
        session->close();
    else
        Simulator::disconnectFault();
}

QVariantList Machine::capabilities() const {
    return {
        QVariantMap{{"name", "XY / GRBL / Referenzfahrt"},
                    {"status", "Protokoll vorhanden · Gerätebetrieb gesperrt"},
                    {"detail", "Nach gemeldeter Kollision sind Maschinenkoordinaten, Grenzen und "
                               "Nullpunktzuordnung nicht unabhängig bestätigt. Keine Hardwarefreigabe."}},
        QVariantMap{{"name", "Kamera und Registrierung"},
                    {"status", "Referenzfoto, keine Live-Anbindung"},
                    {"detail", "Manuelles Bild mit Vierpunktregistrierung verfügbar; Live-Kamera "
                               "und automatische Positionierung nicht abgenommen."}},
        QVariantMap{{"name", "Rotary / Z / Autofokus"},
                    {"status", "Nicht nachgewiesen"},
                    {"detail", "Geräteprofil verwendet nur X/Y. Keine ungeprüften Achsenbefehle."}},
        QVariantMap{{"name", "Oberflächenvermessung / Fokusnachführung"},
                    {"status", "Keine Messhardware"},
                    {"detail", "Keine automatische Höhen- oder Fokuskompensation."}},
        QVariantMap{{"name", "Absaugung separat schalten"},
                    {"status", "Ausgangszuordnung und Luftstrom nicht bestätigt"},
                    {"detail", "Gehäuseabsaugung und Air Assist sind getrennt. Unbekannter "
                               "Zubehörzustand gibt keine Bearbeitung frei."}}};
}

QVariantMap Machine::controllerDiagnosticSnapshot() const {
    // Numeric protocol evidence is available only through the explicit preview
    // workflow. Never collect raw logs, ports, document text or material notes.
    return {{"schema", 1},
            {"controller", live && session ? session->diagnosticSnapshot()
                : QVariantMap{{"supported",false},{"reasonCode","simulator"}}},
            {"facade", QVariantMap{
                {"simulation",simulation()}, {"connected",connected()}, {"ready",ready()},
                {"positionReferenced",positionReferenced()},
                {"motionContractBlocked",!motionSafetyError().isEmpty()},
                {"homingContractBlocked",!homingSafetyError().isEmpty()},
                {"extractionContractBlocked",!extractionSafetyError().isEmpty()},
                {"relativeHardwareStartBlocked",live && document->startMode()!=Document::AbsoluteCoordinates}}}};
}
