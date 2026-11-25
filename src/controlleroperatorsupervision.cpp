#include "controllerruntimeworker.h"
#include "controllerruntimemailbox.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <chrono>
#include <cmath>
#include <QScopedValueRollback>
#include "grblwirenumbers.h"
namespace {
QByteArray contextHash(const QByteArray &source,quint64 epoch,quint64 attempt,const QByteArray &session) {
    QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream<<source<<epoch<<attempt<<session;
    return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();
}
}
QByteArray ControllerRuntimeWorker::supervisionBinding() const {
    if(m_core->operatorLocalPermit && m_core->operatorLocalPermit->frameId==m_local.frameId)
        return contextHash(m_local.frameId+m_local.settingsFingerprint,m_connectionEpoch,0,m_profile.outputFingerprint());
    return contextHash(m_commissioning.fingerprint,m_connectionEpoch,0,m_profile.outputFingerprint());
}
void ControllerRuntimeWorker::discardOperatorWitness(){
    m_operatorWitness.reset();m_operatorAction.clear();m_operatorSource.clear();m_operatorWitnessCoordinate=0;
    m_accessory.discardOperatorWitness();
}
QVariantMap ControllerRuntimeWorker::extractionSupervisionSnapshot() const {
    const auto now=m_accessoryClock.elapsed();
    const auto mode=m_accessory.supervisionMode();
    const bool cycle=mode==AccessoryContract::SupervisionMode::OperatorSupervisedDeviceCycle;
    const auto name=cycle?QString("operatorSupervisedDeviceCycle"):
        mode==AccessoryContract::SupervisionMode::OperatorAttendedExternal?QString("operatorAttendedExternal"):QString("sensorVerified");
    const QVariantList fans={
        QVariantMap{{"role","enclosureExhaust"},{"state","unknown"},{"feedback","notReadBack"},{"independentControl",false}},
        QVariantMap{{"role","laserModuleCooling"},{"state","unknown"},{"feedback","notReadBack"},{"independentControl",false}}};
    const auto startError=attendedStartError(),referenceError=referenceHomeError();
    return {{"referenceReady",referenceError.isEmpty()},{"referenceError",referenceError},
        {"referencePending",m_attendedStart && m_attendedStart->referenceOnly},
        {"startSequencePending",m_attendedStart.has_value()},
        {"startSequencePhase",m_attendedStart?m_attendedStart->phase:QString("idle")},
        {"startSequenceReason",m_attendedStartReason},
        {"singleClickStart",singleClickStartEligible()},{"attendedStartError",startError},{"attendedStartReady",startError.isEmpty()},
        {"mode",name},{"notice",m_accessory.supervisionNotice()},{"reason",m_operatorReason},
        {"pendingAction",m_operatorWitness?m_operatorAction:QString()},
        {"remainingMs",m_operatorWitness?std::max<qint64>(0,m_operatorWitness->expiresAtMs()-now):0},
        {"activeLease",m_accessory.activeLease()},{"airflowMeasured",false},
        {"presenceOnly",cycle},{"requiresExternalExtraction",!cycle},
        {"clearanceRequired",m_accessory.faulted()||m_accessory.releaseRequired()},
        {"deviceFans",fans}};
}
void ControllerRuntimeWorker::operatorData(const QVariantMap &data) {
    const auto op=data.value("op").toString();
    if(op=="withdraw") {
        discardOperatorWitness();m_accessory.operatorNoLongerAttending();
        m_operatorReason=m_accessory.supervisionMode()==AccessoryContract::SupervisionMode::OperatorSupervisedDeviceCycle
            ?"Anwesenheit widerrufen; tatsächlichen Gerätezustand klären und neu einrichten."
            :"Beaufsichtigung oder externe Absaugung widerrufen; neue Einrichtung/Freigabe erforderlich.";
        if((m_core->busy()||m_core->firing) && !m_core->awaitingReset)m_core->stop();
        return;
    }
    if(op=="clearance") {
        if(m_core->ready() && !m_core->firing && !m_accessory.activeLease() && !m_jobActive && !m_focusActive &&
           data.value("confirmed").metaType().id()==QMetaType::Bool && data.value("confirmed").toBool() &&
           m_accessory.releaseAfterSafeClearance(true)) {
            discardOperatorWitness();m_operatorReason=m_accessory.supervisionMode()==AccessoryContract::SupervisionMode::OperatorAttendedExternal
                ?"Sicheren Zustand und Abzug der Dämpfe menschlich bestätigt. Vor Ausgabe erneut attestieren."
                :"Sicheren Zustand am Gerät menschlich geklärt. Vor Ausgabe Anwesenheit erneut bestätigen.";
        } else m_operatorReason="Sichere Freigabe erst im freien Idle/S0 ohne aktive Ausgabe möglich.";
        return;
    }
    const bool local=m_core->operatorLocalPermit && m_core->operatorLocalPermit->frameId==m_local.frameId && m_core->localMotionSafetyError().isEmpty();
    if(!m_core->verified() || (!local && (m_commissioning.stage!="complete" || !m_core->positionReferenced || !m_core->motionSafetyError().isEmpty()))) {
        m_operatorReason="Zuerst diese Gerätesitzung vollständig einrichten und referenzieren.";return;
    }
    refreshCoordinateEpoch();
    if(op=="select" || op=="selectDeviceCycle") {
        if(!m_core->ready() || m_accessory.activeLease() || m_jobActive || m_focusActive)return;
        const auto binding=supervisionBinding();
        if(op=="selectDeviceCycle"?m_accessory.selectOperatorSupervisedDeviceCycle(binding):m_accessory.selectOperatorAttendedExternal(binding)) {
            m_operatorSession=binding;discardOperatorWitness();m_operatorReason=m_accessory.supervisionNotice();
        } else m_operatorReason="Moduswechsel benötigt freien sicheren Zustand und gegebenenfalls ausdrücklich bestätigte Clearance.";
        return;
    }
    const bool cycle=m_accessory.supervisionMode()==AccessoryContract::SupervisionMode::OperatorSupervisedDeviceCycle;
    if((op!="attestDeviceCycle" || !cycle) && (op!="attest" || m_accessory.supervisionMode()!=AccessoryContract::SupervisionMode::OperatorAttendedExternal))return;
    if(m_operatorSession!=supervisionBinding()) {
        discardOperatorWitness();m_operatorReason="Bedienmodus gehört zu einer anderen Geräte-/Settingssitzung; ausdrücklich neu auswählen.";return;
    }
    discardOperatorWitness();
    const auto action=data.value("action").toString();AccessoryContract::OperatorAction kind;
    QByteArray source;
    if(action=="start") {
        kind=AccessoryContract::OperatorAction::StartJob;source=data.value("source").toByteArray();
        if(source.size()!=32+m_profile.outputFingerprint().size() || !source.endsWith(m_profile.outputFingerprint()) || !m_core->ready() || m_accessory.activeLease()){m_operatorReason="Aktueller berechneter Auftrag und freier Idle für Startbestätigung erforderlich.";return;}
    } else if(action=="focus") {
        kind=AccessoryContract::OperatorAction::Focus;source="finite-focus";
        if(!m_core->ready() || m_accessory.activeLease()){m_operatorReason="Freier Idle für Fokusbestätigung erforderlich.";return;}
    } else if(action=="resume") {
        kind=AccessoryContract::OperatorAction::Resume;
        if(!m_core->paused || m_core->controllerState!="Hold" || !m_jobActive || !m_activePrepared || !m_accessory.activeLease() || m_activeOperatorOperation.isEmpty()) {
            m_operatorReason="Resume-Bestätigung benötigt den tatsächlich pausierten aktiven Auftrag.";return;
        }
        source=m_activePrepared->sourceFingerprint()+m_activePrepared->profileFingerprint();
    } else {m_operatorReason="Ausgabeaktion für Bestätigung unbekannt.";return;}
    if(data.value("operatorPresent").metaType().id()!=QMetaType::Bool || (!cycle && data.value("extractionOn").metaType().id()!=QMetaType::Bool))return;
    auto operation=kind==AccessoryContract::OperatorAction::Resume?m_activeOperatorOperation:contextHash(source,m_coordinateEpoch,++m_operatorAttempt,m_operatorSession);
    m_operatorContext={m_operatorSession,operation,kind};
    const auto now=m_accessoryClock.elapsed();
    m_operatorWitness=cycle?m_accessory.submitOperatorPresence(m_operatorContext,data.value("operatorPresent").toBool(),now,now):
        m_accessory.submitOperatorAttestation(m_operatorContext,data.value("extractionOn").toBool(),data.value("operatorPresent").toBool(),now,now);
    if(m_operatorWitness) {
        m_operatorAction=action;m_operatorSource=source;m_operatorWitnessCoordinate=m_coordinateEpoch;
        m_operatorReason=cycle?"Anwesenheit für diese Ausgabeaktion bestätigt. Innerhalb 30 s ausdrücklich starten; Lüfterzustände unbekannt und nicht rücklesbar.":
            "Menschliche Bestätigung für diese Ausgabeaktion erfasst; Luftstrom nicht gemessen. Innerhalb 30 s ausdrücklich starten.";
    } else m_operatorReason=cycle?"Eigene Anwesenheit muss ausdrücklich für diese aktuelle Aktion bestätigt sein.":
        "Externe Absaugung und eigene Anwesenheit müssen ausdrücklich für die aktuelle Aktion bestätigt sein.";
}
bool ControllerRuntimeWorker::beginAccessory(AccessoryContract::OperatorAction action,const QByteArray &requestedSource) {
    if(m_accessory.supervisionMode()==AccessoryContract::SupervisionMode::SensorVerified)
        return action==AccessoryContract::OperatorAction::Resume?m_accessory.preflightError(m_accessoryClock.elapsed()).isEmpty():m_accessory.begin(m_accessoryClock.elapsed());
    refreshCoordinateEpoch();
    if(m_operatorSession!=supervisionBinding()) {
        discardOperatorWitness();return false;
    }
    auto source=requestedSource;
    if(action==AccessoryContract::OperatorAction::Focus)source="finite-focus";
    if(action==AccessoryContract::OperatorAction::Resume && m_activePrepared)source=m_activePrepared->sourceFingerprint()+m_activePrepared->profileFingerprint();
    if(!m_operatorWitness || m_operatorContext.action!=action || m_operatorSource!=source || m_operatorWitnessCoordinate!=m_coordinateEpoch) {
        discardOperatorWitness();m_operatorReason="Frische Bestätigung passt nicht zu genau dieser Ausgabeaktion oder ihrem Koordinatenrahmen.";return false;
    }
    const auto operation=m_operatorContext.operationBinding;
    const bool accepted=m_accessory.beginOperator(m_operatorContext,*m_operatorWitness,m_accessoryClock.elapsed());
    discardOperatorWitness();
    if(accepted){m_activeOperatorOperation=operation;m_operatorPaused=false;
        m_operatorReason=m_accessory.supervisionMode()==AccessoryContract::SupervisionMode::OperatorSupervisedDeviceCycle
            ?"Beaufsichtigter Gerätezyklus aktiv. Lüfterzustände unbekannt; tatsächliche Lüfter beim ersten Materialtest beobachten. Bei fehlender Aufsicht STOPP."
            :"Beaufsichtigte Ausgabe; Luftstrom nicht gemessen. Bei Aufsichts-/Absaugungsverlust STOPP.";}
    else m_operatorReason="Einmalige menschliche Bestätigung nicht mehr gültig; erneut bestätigen.";
    return accepted;
}

bool ControllerRuntimeWorker::singleClickStartEligible() const {
    return m_accessory.singleClickDeviceCycleEligible();
}
bool ControllerRuntimeWorker::noOutputIdle() const {
    return m_core && m_core->connected() && m_core->statusFresh() && m_core->ready() &&
        m_core->controllerState=="Idle" && m_core->knownOutput && m_core->reportedOutput==0 &&
        !m_core->firing && !m_jobActive && !m_focusActive && !m_accessory.activeLease() &&
        !m_accessory.faulted() && !m_accessory.releaseRequired();
}
QString ControllerRuntimeWorker::attendedStartError() const {
    if(m_attendedStart)return "Referenzfahrt und gebundener Auftrag sind bereits eingeplant.";
    if(!singleClickStartEligible())return "Explizit gewählter Zubehörmodus benötigt seinen eigenen Startablauf.";
    const auto error=calibratedHomeError(true);
    if(!error.isEmpty())return error;
    if(m_commissioning.stage!="complete" && !(m_attestedWorkOffsetKnown && m_attestedWorkOffset==m_core->offset) &&
       (m_calibrationCandidate.isEmpty() || QPointF(m_calibrationCandidate.value("workOriginX").toDouble(),
        m_calibrationCandidate.value("workOriginY").toDouble())!=m_core->offset))
        return "Geänderten G54-Arbeitsursprung nach Referenzfahrt ausdrücklich prüfen.";
    return {};
}
QString ControllerRuntimeWorker::referenceHomeError() const {
    return calibratedHomeError(false);
}
QString ControllerRuntimeWorker::calibratedHomeError(bool allowApproach) const {
    if(m_attendedStart)return "Gebundene Referenzfahrt läuft bereits.";
    if(m_accessory.faulted() || m_accessory.releaseRequired())return "Tatsächlichen Gerätezustand klären und sichere Freigabe ausdrücklich bestätigen.";
    const auto &c=m_commissioning;
    if(!m_core || (c.stage!="complete" && c.stage!="savedHomeReady" && !(allowApproach && c.stage=="savedApproachReady")) ||
       !m_core->setupReadback.valid || c.fingerprint!=m_core->setupReadback.fingerprint ||
       !c.limits.isValid() || !std::isfinite(c.home.x()) || !std::isfinite(c.home.y()))
        return "Abgeglichene vollständige Kalibrierung für die Startreferenz erforderlich.";
    if(!m_core->setupReadyForAttestation() || m_core->firing || m_jobActive || m_focusActive || m_accessory.activeLease())
        return "Frischer ruhender S0-Zustand ohne aktive Ausgabe erforderlich.";
    if(m_attestedWorkOffsetKnown ? m_core->offset!=m_attestedWorkOffset :
       (c.stage=="complete" || m_core->offset!=m_core->setupReadback.g54))
        return "Aktueller G54 gehört nicht zur ausdrücklich bestätigten Kalibrierung.";
    return {};
}
QString ControllerRuntimeWorker::attendedStartInvalidationReason(bool atHomeWrite) const {
    const auto deny=[](const char *detail){return QString("Startfolge widerrufen: ")+QString::fromUtf8(detail);};
    if(!m_attendedStart)return deny("gebundene Operation fehlt.");
    const auto &p=*m_attendedStart;const auto &c=m_commissioning;
    const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if(now<p.admittedAt)return deny("monotone Zeit vor Zulassung.");
    if(now>p.deadline)return deny("120-Sekunden-Gesamtfrist abgelaufen.");
    if(p.connection!=m_connectionEpoch)return deny("Verbindungsepoche verändert.");
    if(p.generation!=m_mailbox->generation.load())return deny("globale STOPP-/Widerrufsgeneration verändert.");
    if(!p.referenceOnly && p.sourceGeneration!=m_mailbox->startSequenceGeneration.load())return deny("Auftragsquelle verändert.");
    if(p.window!=m_mailbox->sequenceContextEpoch.load())return deny("globaler Fenster-/Eingabekontext verloren.");
    if(p.binding!=m_calibrationBinding)return deny("Kalibrier-Gerätebindung verändert.");
    if(p.profile!=m_profile.outputFingerprint())return deny("Geräteprofil verändert.");
    if(p.settings!=c.fingerprint)return deny("Kalibrier-Konfigurationsbindung verändert.");
    if(!m_core->setupReadback.valid)return deny("Controllerreadback nicht mehr gültig.");
    if(p.settings!=m_core->setupReadback.fingerprint)return deny("Controllerkonfiguration verändert.");
    if(p.limits!=c.limits)return deny("bestätigter Innenraum verändert.");
    if(p.home!=c.home)return deny("kalibrierter Homepunkt verändert.");
    if(p.offset!=m_core->offset)return deny("G54/WCO verändert.");
    if(!m_core->connected())return deny("Controllerverbindung geschlossen.");
    if(m_core->awaitingReset)return deny("Reset aktiv.");
    if(m_core->handshaking)return deny("Handshake aktiv.");
    if(m_core->paused)return deny("unerwarteter Hold während Startreferenz.");
    if(m_core->firing)return deny("unerwartete Fokusausgabe aktiv.");
    if(m_accessory.faulted())return deny("Zubehörstörung aktiv.");
    if(m_accessory.releaseRequired())return deny("sichere Zubehörfreigabe erforderlich.");
    if(p.phase!="starting" && m_accessory.activeLease())return deny("unerwartete aktive Zubehörlease.");
    if(p.phase!="starting" && m_jobActive)return deny("unerwarteter aktiver Auftrag.");
    if(p.phase!="starting" && m_focusActive)return deny("unerwartete aktive Fokuslease.");
    if(m_mailbox->stop.load())return deny("STOPP angefordert.");
    if(m_mailbox->close.load())return deny("Schließen angefordert.");
    if(m_mailbox->shutdown.load())return deny("Programmende angefordert.");
    if(atHomeWrite) {
        if(p.phase!="homing")return deny("Home-Schreibphase stimmt nicht.");
        if(p.homeWritten)return deny("Home bereits einmal gesendet.");
        if(!m_core->statusFresh())return deny("Controllerstatus vor Home veraltet.");
        if(!m_core->knownOutput)return deny("Ausgangsleistung vor Home unbekannt.");
        if(m_core->reportedOutput!=0)return deny("Ausgangsleistung vor Home nicht null.");
        if(m_core->activeInputs)return deny("Sperreingang vor Home aktiv.");
        if(m_core->controllerState!="Idle" && m_core->controllerState!="Alarm")return deny("Controllerzustand vor Home nicht zulässig.");
    }
    return {};
}
bool ControllerRuntimeWorker::validateAttendedStart(bool atHomeWrite) {
    const auto error=attendedStartInvalidationReason(atHomeWrite);
    if(!error.isEmpty())m_attendedStartFailure=error;
    return error.isEmpty();
}
bool ControllerRuntimeWorker::beginAttendedStart(const ControllerRuntimeRequest &request) {
    const auto error=attendedStartError();
    if(!error.isEmpty()){m_operatorReason=error;return false;}
    const auto &job=request.prepared;const auto &c=m_commissioning;
    if(!job || !job->valid() || job->profileFingerprint()!=m_profile.outputFingerprint() ||
       job->driverId()!=m_profile.driverId() || job->transport()!=m_profile.transport() ||
       job->mediaType()!="text/x-gcode")return false;
    // Validate the frozen emitted targets against the interior before any Home.
    const auto bounds=job->workBounds();
    for(const auto point:{bounds.topLeft(),bounds.topRight(),bounds.bottomLeft(),bounds.bottomRight(),job->firstApproach()}) {
        const QPointF machine(GrblWireNumbers::coordinate(point.x())+m_core->offset.x(),
                              GrblWireNumbers::coordinate(point.y())+m_core->offset.y());
        if(!std::isfinite(point.x()) || !std::isfinite(point.y()) || !std::isfinite(machine.x()) || !std::isfinite(machine.y()) ||
           machine.x()<c.limits.left() || machine.x()>c.limits.right() || machine.y()<c.limits.top() || machine.y()>c.limits.bottom()) {
            m_operatorReason="Auftrag liegt mit aktuellem WCO außerhalb des bestätigten Fahrraums.";return false;
        }
    }
    return commitCalibratedReference(request,false);
}
bool ControllerRuntimeWorker::beginReferenceHome(const ControllerRuntimeRequest &request) {
    const auto error=referenceHomeError();
    if(!error.isEmpty()){m_attendedStartReason=error;return false;}
    return commitCalibratedReference(request,true);
}
bool ControllerRuntimeWorker::commitCalibratedReference(const ControllerRuntimeRequest &request,bool referenceOnly) {
    const auto &c=m_commissioning;
    const auto proof=request.input.proof;
    const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const auto expected=referenceOnly?ControllerNativeAction::ReferenceHome:ControllerNativeAction::StartAttended;
    if(!proof || proof->action!=expected || proof->observedAt<=0 ||
       proof->observedAt>now || proof->expiresAt<now || now-proof->observedAt>250 ||
       !proof->contextToken || !proof->contextToken->valid || !checkClockContinuity())return false;
    AttendedStartOperation operation;
    operation.referenceOnly=referenceOnly;operation.prepared=referenceOnly?PreparedControllerJob::Ptr{}:request.prepared;operation.connection=m_connectionEpoch;operation.generation=m_mailbox->generation.load();
    operation.sourceGeneration=m_mailbox->startSequenceGeneration.load();operation.window=m_mailbox->sequenceContextEpoch.load();
    operation.observedAt=proof->observedAt;operation.admittedAt=now;operation.deadline=proof->observedAt+120000;
    operation.relativeAdmittedAt=m_accessoryClock.elapsed();operation.relativeObservedAt=operation.relativeAdmittedAt-(now-proof->observedAt);
    operation.binding=m_calibrationBinding;operation.profile=m_profile.outputFingerprint();operation.settings=c.fingerprint;
    operation.offset=m_core->offset;operation.home=c.home;operation.limits=c.limits;
    m_attendedStartFailure.clear();
    m_attendedStart=std::move(operation);m_attendedStartReason=referenceOnly?"Laserfreie Referenzfahrt bestätigt; danach kein Auftrag und keine weitere Anfahrt.":"Start bestätigt: zuerst kalibrierte Referenzfahrt, dann genau dieser Auftrag.";
    m_dispatchProof.reset(); // This is a committed bounded operation, never a renewed native event.
    discardConnectionHomePermit();m_connectionHomeStarted=false;
    m_commissioning.restoring=true;
    // Homing may end at the attested home outside the normal interior. Its
    // own typed phase re-establishes only the attested approach envelope.
    m_core->commissionedModalContract=false;
    startCalibratedHome();
    return m_attendedStart.has_value();
}
void ControllerRuntimeWorker::scheduleAttendedStart() {
    if(!m_attendedStart || m_attendedStartUpdateQueued)return;
    m_attendedStartUpdateQueued=true;
    // Core completion logs emit changed() inside its status parser. Do not
    // enqueue a new motion stream reentrantly before that parser has returned.
    QMetaObject::invokeMethod(this,[this]{m_attendedStartUpdateQueued=false;updateAttendedStart();publish();},Qt::QueuedConnection);
}
void ControllerRuntimeWorker::updateAttendedStart() {
    if(!m_attendedStart || m_updatingAttendedStart)return;
    QScopedValueRollback<bool> guard(m_updatingAttendedStart,true);
    if(!validateAttendedStart()) {cancelCommissioning(m_attendedStartFailure);return;}
    auto &p=*m_attendedStart;auto &c=m_commissioning;
    if(m_core->busy())return;
    if(p.referenceOnly && (c.stage=="complete" || c.stage=="savedApproachReady" || c.stage=="restoredOriginReview")) {
        if(m_core->ready() && m_core->verified() && m_core->positionReferenced)
            discardAttendedStart(c.stage=="complete"?"Referenzpunkt frisch bestätigt; kein Auftrag gestartet.":c.stage=="restoredOriginReview"?"Referenzpunkt bestätigt; geänderten G54-Arbeitsursprung ausdrücklich prüfen.":"Referenzpunkt außerhalb des Innenraums bestätigt; keine weitere Bewegung. Start benötigt gebundenen sicheren Anlauf.");
        return;
    }
    if(c.stage=="savedApproachReady") {
        if(!m_core->ready() || !m_core->verified())return;
        const QPointF target=c.limits.topLeft(),work=target-m_core->offset;
        if(!m_core->insideMachineBed(work)){cancelCommissioning("Startanlauf liegt gerundet außerhalb des bestätigten Anfahrtsraums.");return;}
        p.phase="approaching";c.stage="restoringApproach";c.target=target;
        c.deadline=p.deadline;c.reason="Gebundener Start: langsamer laserfreier Anlauf zum geprüften Innenraum.";
        if(!m_core->commands({"M5 S0","G21 G90 G54",QString("G1 X%1 Y%2 F300 S0").arg(GrblWireNumbers::format(work.x()),GrblWireNumbers::format(work.y()))}))
            cancelCommissioning("Gebundener Startanlauf nicht angenommen.");
        return;
    }
    if(c.stage!="complete")return;
    if(!m_core->ready() || !m_core->verified() || !m_core->positionReferenced ||
       !m_core->motionSafetyError().isEmpty() || !m_core->insideReportedMachineBed())return;
    p.phase="starting";
    const auto binding=supervisionBinding();
    if(!m_accessory.selectOperatorSupervisedDeviceCycle(binding)){cancelCommissioning("Beaufsichtigter Start benötigt störungsfreien Zubehörzustand.");return;}
    m_operatorSession=binding;discardOperatorWitness();
    const auto source=p.prepared->sourceFingerprint()+p.prepared->profileFingerprint();
    const auto op=contextHash(source,m_coordinateEpoch,++m_operatorAttempt,binding);
    const AccessoryContract::OperatorContext context{binding,op,AccessoryContract::OperatorAction::StartJob};
    if(!m_accessory.beginOperatorWithCommittedStartIntent(context,p.relativeObservedAt,p.relativeAdmittedAt,m_accessoryClock.elapsed())) {
        cancelCommissioning("Ursprünglich gebundener Start ist für die beaufsichtigte Ausgabe nicht mehr gültig.");return;
    }
    m_activeOperatorOperation=op;m_operatorPaused=false;
    m_activePrepared=p.prepared;m_jobActive=true;
    if(!m_core->startPrepared(*m_activePrepared,p.home)) {
        m_jobActive=false;m_accessory.transition(AccessoryContract::Event::Fault);
        retirePrepared();cancelCommissioning("Gebundener Auftrag nach Referenzfahrt nicht angenommen.");return;
    }
    discardAttendedStart("Referenz bestätigt; gebundener Auftrag läuft und kehrt nach Erfolg zum Maschinen-Home zurück.");
}
