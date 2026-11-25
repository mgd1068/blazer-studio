#include "controllerruntimeworker.h"
#include "controllerruntimemailbox.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <chrono>
#include <cmath>
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
    const auto startError=attendedStartError();
    return {{"singleClickStart",singleClickStartEligible()},{"attendedStartError",startError},{"attendedStartReady",startError.isEmpty()},
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
    if(!singleClickStartEligible())return "Explizit gewählter Zubehörmodus benötigt seinen eigenen Startablauf.";
    if(m_accessory.faulted() || m_accessory.releaseRequired())return "Tatsächlichen Gerätezustand klären und sichere Freigabe ausdrücklich bestätigen.";
    if(!m_core || m_commissioning.stage!="complete" || !m_core->positionReferenced)
        return "Gerät zuerst vollständig einrichten und frisch referenzieren.";
    if(!m_core->verified() || !noOutputIdle())return "Frischer bestätigter Idle/S0 ohne aktive Ausgabe erforderlich.";
    if(const auto error=m_core->motionSafetyError();!error.isEmpty())return error;
    if(!m_core->insideReportedMachineBed())return "Gemeldeter physischer Kopf liegt außerhalb des bestätigten Fahrraums.";
    return {};
}
bool ControllerRuntimeWorker::beginAttendedStart(const ControllerRuntimeRequest &request) {
    const auto error=attendedStartError();
    if(!error.isEmpty()){m_operatorReason=error;return false;}
    const auto &job=request.prepared;
    if(!job || !job->valid() || job->profileFingerprint()!=m_profile.outputFingerprint() ||
       job->driverId()!=m_profile.driverId() || job->transport()!=m_profile.transport() ||
       job->mediaType()!="text/x-gcode")return false;
    const auto bounds=job->workBounds();
    for(const auto point:{bounds.topLeft(),bounds.topRight(),bounds.bottomLeft(),bounds.bottomRight(),job->firstApproach()}) {
        if(!std::isfinite(point.x()) || !std::isfinite(point.y()) || !m_core->insideMachineBed(point)) {
            m_operatorReason="Auftrag liegt mit aktuellem WCO außerhalb des bestätigten Fahrraums.";return false;
        }
    }
    const auto proof=request.input.proof;
    const auto absoluteNow=std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if(!proof || proof->action!=ControllerNativeAction::StartAttended ||
       proof->observedAt<=0 || proof->observedAt>absoluteNow || proof->expiresAt<absoluteNow ||
       absoluteNow-proof->observedAt>250 || !proof->contextToken || !proof->contextToken->valid ||
       m_mailbox->stop.load() || m_mailbox->close.load() || !checkClockContinuity())return false;
    refreshCoordinateEpoch();
    const auto binding=supervisionBinding();
    if(!m_accessory.selectOperatorSupervisedDeviceCycle(binding))return false;
    m_operatorSession=binding;discardOperatorWitness();
    const auto source=job->sourceFingerprint()+job->profileFingerprint();
    const auto operation=contextHash(source,m_coordinateEpoch,++m_operatorAttempt,binding);
    const AccessoryContract::OperatorContext context{binding,operation,AccessoryContract::OperatorAction::StartJob};
    // Preserve the original OS event's age across distinct monotonic origins.
    const auto relativeNow=m_accessoryClock.elapsed();
    const auto observed=relativeNow-(absoluteNow-proof->observedAt);
    if(!m_accessory.beginOperatorWithStartIntent(context,observed,relativeNow))return false;
    m_activeOperatorOperation=operation;m_operatorPaused=false;
    m_operatorReason="Beaufsichtigter Gerätezyklus durch frischen Start bestätigt. Lüfterzustände unbekannt und nicht rücklesbar.";
    return true;
}
