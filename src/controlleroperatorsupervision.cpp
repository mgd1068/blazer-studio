#include "controllerruntimeworker.h"
#include "controllerruntimemailbox.h"
#include <QCryptographicHash>
#include <QDataStream>
namespace {
QByteArray contextHash(const QByteArray &source,quint64 epoch,quint64 attempt,const QByteArray &session) {
    QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream<<source<<epoch<<attempt<<session;
    return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();
}
}
void ControllerRuntimeWorker::discardOperatorWitness(){
    m_operatorWitness.reset();m_operatorAction.clear();m_operatorSource.clear();m_operatorWitnessCoordinate=0;
    m_accessory.discardOperatorWitness();
}
QVariantMap ControllerRuntimeWorker::extractionSupervisionSnapshot() const {
    const auto now=m_accessoryClock.elapsed();
    return {{"mode",m_accessory.supervisionMode()==AccessoryContract::SupervisionMode::OperatorAttendedExternal?"operatorAttendedExternal":"sensorVerified"},
        {"notice",m_accessory.supervisionNotice()},{"reason",m_operatorReason},
        {"pendingAction",m_operatorWitness?m_operatorAction:QString()},
        {"remainingMs",m_operatorWitness?std::max<qint64>(0,m_operatorWitness->expiresAtMs()-now):0},
        {"activeLease",m_accessory.activeLease()},{"airflowMeasured",false}};
}
void ControllerRuntimeWorker::operatorData(const QVariantMap &data) {
    const auto op=data.value("op").toString();
    if(op=="withdraw") {
        discardOperatorWitness();m_accessory.operatorNoLongerAttending();
        m_operatorReason="Beaufsichtigung oder externe Absaugung widerrufen; neue Einrichtung/Freigabe erforderlich.";
        if((m_core->busy()||m_core->firing) && !m_core->awaitingReset)m_core->stop();
        return;
    }
    if(op=="clearance") {
        if(m_core->ready() && !m_core->firing && !m_accessory.activeLease() && !m_jobActive && !m_focusActive &&
           data.value("confirmed").metaType().id()==QMetaType::Bool && data.value("confirmed").toBool() &&
           m_accessory.releaseAfterSafeClearance(true)) {
            discardOperatorWitness();m_operatorReason="Sicheren Zustand und Abzug der Dämpfe menschlich bestätigt. Vor Ausgabe erneut attestieren.";
        } else m_operatorReason="Sichere Freigabe erst im freien Idle/S0 ohne aktive Ausgabe möglich.";
        return;
    }
    if(m_commissioning.stage!="complete" || !m_core->verified() || !m_core->positionReferenced || !m_core->motionSafetyError().isEmpty()) {
        m_operatorReason="Zuerst diese Gerätesitzung vollständig einrichten und referenzieren.";return;
    }
    refreshCoordinateEpoch();
    if(op=="select") {
        if(!m_core->ready() || m_accessory.activeLease() || m_jobActive || m_focusActive)return;
        const auto binding=contextHash(m_commissioning.fingerprint,m_connectionEpoch,0,m_profile.outputFingerprint());
        if(m_accessory.selectOperatorAttendedExternal(binding)) {
            m_operatorSession=binding;discardOperatorWitness();m_operatorReason=m_accessory.supervisionNotice();
        } else m_operatorReason="Moduswechsel benötigt freien sicheren Zustand und gegebenenfalls ausdrücklich bestätigte Clearance.";
        return;
    }
    if(op!="attest" || m_accessory.supervisionMode()!=AccessoryContract::SupervisionMode::OperatorAttendedExternal)return;
    if(m_operatorSession!=contextHash(m_commissioning.fingerprint,m_connectionEpoch,0,m_profile.outputFingerprint())) {
        discardOperatorWitness();m_operatorReason="Absaugungsmodus gehört zu einer anderen Geräte-/Settingssitzung; ausdrücklich neu auswählen.";return;
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
    if(data.value("extractionOn").metaType().id()!=QMetaType::Bool || data.value("operatorPresent").metaType().id()!=QMetaType::Bool)return;
    auto operation=kind==AccessoryContract::OperatorAction::Resume?m_activeOperatorOperation:contextHash(source,m_coordinateEpoch,++m_operatorAttempt,m_operatorSession);
    m_operatorContext={m_operatorSession,operation,kind};
    const auto now=m_accessoryClock.elapsed();
    m_operatorWitness=m_accessory.submitOperatorAttestation(m_operatorContext,data.value("extractionOn").toBool(),data.value("operatorPresent").toBool(),now,now);
    if(m_operatorWitness) {
        m_operatorAction=action;m_operatorSource=source;m_operatorWitnessCoordinate=m_coordinateEpoch;
        m_operatorReason="Menschliche Bestätigung für diese Ausgabeaktion erfasst; Luftstrom nicht gemessen. Innerhalb 30 s ausdrücklich starten.";
    } else m_operatorReason="Externe Absaugung und eigene Anwesenheit müssen ausdrücklich für die aktuelle Aktion bestätigt sein.";
}
bool ControllerRuntimeWorker::beginAccessory(AccessoryContract::OperatorAction action,const QByteArray &requestedSource) {
    if(m_accessory.supervisionMode()==AccessoryContract::SupervisionMode::SensorVerified)
        return action==AccessoryContract::OperatorAction::Resume?m_accessory.preflightError(m_accessoryClock.elapsed()).isEmpty():m_accessory.begin(m_accessoryClock.elapsed());
    refreshCoordinateEpoch();
    if(m_operatorSession!=contextHash(m_commissioning.fingerprint,m_connectionEpoch,0,m_profile.outputFingerprint())) {
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
    if(accepted){m_activeOperatorOperation=operation;m_operatorPaused=false;m_operatorReason="Beaufsichtigte Ausgabe; Luftstrom nicht gemessen. Bei Aufsichts-/Absaugungsverlust STOPP.";}
    else m_operatorReason="Einmalige menschliche Bestätigung nicht mehr gültig; erneut bestätigen.";
    return accepted;
}
