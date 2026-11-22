#include "controllerruntimeworker.h"
#include "controllerruntimemailbox.h"
#include "grblwirenumbers.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QRandomGenerator>
#include <QScopedValueRollback>
#include <chrono>
#include <cmath>
namespace {
qint64 localNow(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
QByteArray nonce(){return QCryptographicHash::hash(QByteArray::number(QRandomGenerator::global()->generate64())+QByteArray::number(localNow()),QCryptographicHash::Sha256).toHex();}
QVariantMap distances(ManualTravelCheck::Distances a){return {{"negativeX",a.negativeX},{"positiveX",a.positiveX},{"negativeY",a.negativeY},{"positiveY",a.positiveY}};}
bool sameLocal(QPointF a,QPointF b){return std::abs(a.x()-b.x())<=1e-6&&std::abs(a.y()-b.y())<=1e-6;}
}
QString ControllerRuntimeWorker::runtimeMotionError() const {
    if(m_core->localExecutionActive) {
        if(!m_core->operatorLocalPermit || m_core->operatorLocalPermit->frameId!=m_local.frameId)return "Lokale Ausführungsautorität entwertet.";
        if(!m_core->verified())return "Lokale Controllerprüfung veraltet.";
        return m_core->localMotionSafetyError();
    }
    if(!m_core->verified()||!m_core->positionReferenced)return "Globale Controllerreferenz fehlt.";
    return m_core->motionSafetyError();
}
void ControllerRuntimeWorker::invalidateLocalFrame(const QString &reason) {
    if(!m_local.engaged())return;
    const bool moving=m_core->localExecutionActive||m_core->firing;
    const bool capture=m_core->workZero && m_core->workZero->operatorLocal;
    m_local.phase="invalid";m_local.reason=reason;m_local.attested=false;m_local.approvedArtifact.clear();m_local.approvalId.clear();
    m_core->operatorLocalPermit.reset();m_core->localCaptureResult.reset();m_core->localExecutionActive=false;m_core->localExecutionKind=GrblSession::LocalExecutionKind::None;
    m_localFirstMotionDue=-1;discardOperatorWitness();
    retireLocalJob(std::move(m_localCandidate));retireLocalJob(std::move(m_localActive));
    if((moving||capture)&&m_core->connected()&&!m_core->awaitingReset)m_core->stop();
    else if(m_core->setupReadback.pending&&m_core->setupReadback.operatorLocal) {
        // Abort only readonly queries; do not introduce an unrequested reset
        // before startup contents have been established.
        m_core->queue.clear();m_core->queueHead=0;m_core->waitingAck=m_core->waitingIdle=m_core->handshaking=false;
        m_core->inFlight.clear();m_core->sent=m_core->rxBytes=0;m_core->clearAckDeadline();m_core->timeout.stop();
        m_core->setupReadback.pending=false;m_core->setupReadback.valid=false;m_core->connectionVerified=false;m_core->poll.start();
    }
}
QVariantMap ControllerRuntimeWorker::localFrameSnapshot() const {
    const auto &f=m_local;const auto a=f.available;
    const bool known=m_core->operatorLocalPermit && m_core->operatorLocalPermit->frameId==f.frameId && m_core->localMotionSafetyError().isEmpty();
    const auto delta=known?m_core->reportedMachinePosition-f.counterAtCapture:QPointF();
    const ManualTravelCheck::Distances usable{a.negativeX-2,a.positiveX-2,a.negativeY-2,a.positiveY-2};
    const ManualTravelCheck::Distances atHead{a.negativeX+delta.x(),a.positiveX-delta.x(),a.negativeY+delta.y(),a.positiveY-delta.y()};
    QVariantMap result{{"phase",f.phase},{"reason",f.reason},{"frameId",QString::fromLatin1(f.frameId)},
        {"approvalId",QString::fromLatin1(f.approvalId)},{"coordinateEpoch",QVariant::fromValue(m_coordinateEpoch)},
        {"sourceFingerprint",m_localCandidate?QString::fromLatin1(m_localCandidate->sourceFingerprint().toHex()):QString()},
        {"localPositionKnown",known},{"localX",delta.x()},{"localY",delta.y()},
        {"virtualHeadX",f.confirmedHead.x()},{"virtualHeadY",f.confirmedHead.y()},
        {"planHeadX",m_core->position.x()},{"planHeadY",m_core->position.y()},
        {"measuredAvailable",distances(a)},{"available",distances(m_localCandidate?m_localCandidate->available():a)},{"planAvailable",distances(atHead)},{"usableAfterMargin",distances(m_localCandidate?m_localCandidate->usableAfterMargin():usable)},
        {"resultRevision",QVariant::fromValue(f.resultRevision)},{"lastRequestAccepted",f.lastRequestAccepted},{"marginMm",2.},{"prepared",bool(m_localCandidate&&m_localCandidate->valid())},
        {"approved",bool(m_localCandidate&&f.approvedArtifact==m_localCandidate->fingerprint())},
        {"fits",bool(m_localCandidate&&m_localCandidate->valid())},{"startReady",false},
        {"motorIdleDelayMs",m_core->setupReadback.settings.contains(1)?m_core->setupReadback.settings.value(1):-1.},
        {"doorSubstate",m_core->doorSubstate}};
    result["frameReady"]=known && m_localCandidate && !m_localCandidate->frameLines().isEmpty() &&
        (f.phase=="travelReview"||f.phase=="ready") && m_core->ready() && m_core->verified() && !m_accessory.activeLease();
    result["frameReason"]=m_localCandidate?m_localCandidate->frameError():QString("Lokalen Auftrag zuerst vorbereiten.");
    result["resumeReady"]=known && f.phase=="paused" && m_core->paused && m_core->controllerState=="Hold" && m_core->verified() &&
        ((m_localFrameRun && m_localActive && !m_localActive->frameLines().isEmpty() && m_core->knownOutput && m_core->reportedOutput==0 && !m_accessory.activeLease()) || (m_operatorWitness && m_operatorAction=="resume" && m_accessory.preflightError(m_accessoryClock.elapsed()).isEmpty()));
    if(m_localCandidate)result["requiredActualWire"]=distances(m_localCandidate->requiredUnion());
    else result["requiredActualWire"]=QVariantMap{};
    result["startReady"]=known && f.phase=="ready" && result["approved"].toBool() && m_core->ready() && m_core->verified() &&
        m_accessory.preflightError(m_accessoryClock.elapsed()).isEmpty() && !m_accessory.activeLease();
    return result;
}
void ControllerRuntimeWorker::localData(const ControllerRuntimeRequest &request) {
    const auto data=request.bounds;const auto op=data.value("op").toString();auto &f=m_local;f.resultRevision=request.id;f.lastRequestAccepted=true;
    const auto reject=[&](const QString &why){f.reason=why;f.lastRequestAccepted=false;};
    if(op=="cancel") {invalidateLocalFrame("Lokalen Rahmen abgebrochen; globale Referenz bleibt unbekannt.");f.phase="inactive";return;}
    if(op=="begin") {
        if(m_core->busy()||m_core->firing||m_accessory.activeLease()||m_jobActive||m_focusActive||!m_core->connected()||!m_core->statusFresh()||!m_core->knownOutput||m_core->reportedOutput!=0) {reject("Vor Handpositionierung erst aktiven Auftrag STOPPEN und frischen ruhenden S0-Zustand abwarten.");return;}
        invalidateLocalFrame("Neuer manueller Bezug.");f={};f.resultRevision=request.id;f.frameId=nonce();f.connectionEpoch=m_connectionEpoch;f.startupSequence=m_core->startupSequence;
        f.phase="reading";f.deadline=localNow()+15000;f.reason="Nur Controllerdaten lesen; globale Referenz für Handpositionierung verwerfen.";
        m_commissioning.stage="inactive";m_core->commissionedEnvelope.reset();m_core->commissionedModalContract=m_core->commissionedHomingContract=m_core->commissionedOverrideContract=false;m_core->positionReferenced=false;
        if(!m_core->beginSetupReadback(true))invalidateLocalFrame("Lokaler Datenreadback konnte nicht begonnen werden.");
        return;
    }
    if(!f.engaged()||f.phase=="invalid"){reject("Manuelle Positionierung zuerst beginnen.");return;}
    if(op=="invalidateCandidate") {
        f.approvalId.clear();f.approvedArtifact.clear();retireLocalJob(std::move(m_localCandidate));
        if(m_core->operatorLocalPermit && !m_core->localExecutionActive){f.phase="preparing";f.reason="Aktuellen lokalen Auftrag vollständig vorbereiten und Wirewege prüfen.";}
        return;
    }
    if(op=="candidate") {
        if(data.value("frameId").toByteArray()!=f.frameId||!m_core->operatorLocalPermit)return;
        retireLocalJob(std::move(m_localCandidate));f.approvedArtifact.clear();f.approvalId.clear();
        if(!request.localPrepared || !request.localPrepared->valid()) {
            if(!m_core->localExecutionActive)f.phase="travelReview";
            f.reason=data.value("error").toString().left(1200);return;
        }
        const auto p=request.localPrepared;
        if(p->profileFingerprint()!=m_profile.outputFingerprint() || !sameLocal(p->headAnchor(),m_core->position) || !m_core->localMotionSafetyError().isEmpty()) {reject("Planungsanker oder lokaler Rahmen verändert; neu vorbereiten.");return;}
        const auto delta=m_core->reportedMachinePosition-f.counterAtCapture;
        const auto supplied=p->available();
        if(std::abs(supplied.negativeX-(f.available.negativeX+delta.x()))>1e-6||std::abs(supplied.positiveX-(f.available.positiveX-delta.x()))>1e-6||std::abs(supplied.negativeY-(f.available.negativeY+delta.y()))>1e-6||std::abs(supplied.positiveY-(f.available.positiveY-delta.y()))>1e-6){reject("Vorbereitete Messdaten passen nicht zum tatsächlich erfassten lokalen Raum.");return;}
        const auto b=p->actualAbsoluteWireBounds();
        for(auto point:{b.topLeft(),b.topRight(),b.bottomLeft(),b.bottomRight(),p->firstApproach()})if(!m_core->localTargetAllowed(point)){reject("Tatsächlich formatierter Verfahrweg passt nicht zu den gemessenen lokalen Restwegen.");return;}
        m_localCandidate=p;f.approvalId=nonce();if(!m_core->localExecutionActive)f.phase="travelReview";
        f.reason="Alle Raw-/Wire-Verfahrwege geprüft. Genau diesen Auftrag und Restwegvergleich ausdrücklich bestätigen.";return;
    }
    if(m_core->busy()||m_core->firing||m_accessory.activeLease()||m_jobActive||m_focusActive){reject("Lokale Daten während aktiver Geräteaktion nicht ändern.");return;}
    if(op=="travel" && (f.phase=="positioning"||f.phase=="captureReady")) {
        ManualTravelCheck::Distances a;
        double *targets[]{&a.negativeX,&a.positiveX,&a.negativeY,&a.positiveY};const QStringList keys{"negativeX","positiveX","negativeY","positiveY"};
        for(int i=0;i<4;++i){const auto v=data.value(keys[i]);if((v.metaType().id()!=QMetaType::Double&&v.metaType().id()!=QMetaType::Int)||!std::isfinite(v.toDouble())||v.toDouble()<2||v.toDouble()>2000){reject("Vier gemessene endliche Restwege von jeweils mindestens 2 mm erforderlich.");return;}*targets[i]=v.toDouble();}
        if(a.negativeX+a.positiveX>m_profile.workArea().width()||a.negativeY+a.positiveY>m_profile.workArea().height()){reject("Gegenüberliegende Restwege überschreiten die nominale Achslänge; Messung/Achszuordnung prüfen.");return;}
        f.available=a;f.hasTravel=true;f.attested=false;f.frameId=nonce();f.phase="positioning";f.reason="Restwege erfasst; feste 2-mm-Reserve wird abgezogen. Tatsächliche Motorfreigabe, Achsen/Raum, Kopfstillstand und geschlossene Haube bestätigen.";return;
    }
    if(op=="attest" && f.phase=="positioning" && f.hasTravel) {
        if(data.value("frameId").toByteArray()!=f.frameId)return;
        for(const auto key:{"motorsFree","measuredClear","headStationary","lidClosed"})if(data.value(key).metaType().id()!=QMetaType::Bool||!data.value(key).toBool()){reject("Alle vier tatsächlichen Beobachtungen ausdrücklich bestätigen.");return;}
        if(!m_core->setupReadback.valid||m_core->setupReadback.settings.value(20)!=0){reject("Frischer vollständiger Readback mit deaktivierten globalen Softlimits erforderlich; keine automatische Settingsänderung.");return;}
        if(m_core->controllerState!="Idle" && !(m_core->controllerState=="Door"&&m_core->doorSubstate==0)){reject("Haube/Controller nicht für bewusste lokale Aufnahme bereit.");return;}
        f.attested=true;f.phase="captureReady";f.reason="Vorbereitung abgeschlossen. Native Aufnahme setzt Controller bewusst zurück und bestätigt G54 ohne XY-Bewegung; kein automatisches Resume/Homing.";return;
    }
    if(op=="approve" && f.phase=="travelReview" && m_localCandidate && data.value("approvalId").toByteArray()==f.approvalId && !f.approvalId.isEmpty()) {
        if(!m_core->ready()||!m_core->verified()||!m_core->localMotionSafetyError().isEmpty()||!sameLocal(m_core->position,m_localCandidate->headAnchor())){reject("Lokaler Startpunkt oder Controllerzustand nicht aktuell bestätigt.");return;}
        f.approvedArtifact=m_localCandidate->fingerprint();f.phase="ready";f.reason="Lokaler Plan und Restwege bestätigt; Absaugung/Aufsicht separat bestätigen, dann ausdrücklich starten.";return;
    }
    reject("Dieser lokale Einrichtungsschritt ist in der aktuellen Phase nicht zulässig.");
}
void ControllerRuntimeWorker::localMotion(const ControllerRuntimeRequest &request) {
    auto &f=m_local;
    if(request.bounds.value("frameId").toByteArray()!=f.frameId)return;
    if(request.kind==ControllerRuntimeRequestKind::LocalCapture) {
        if(f.phase!="captureReady"||!f.attested||!f.hasTravel||m_core->busy()||m_core->firing||m_accessory.activeLease()||m_jobActive||m_focusActive||!m_core->setupReadback.valid||m_core->setupReadback.startup.size()!=2||!m_core->statusFresh()||!m_core->knownOutput||m_core->reportedOutput!=0||
           (m_core->controllerState!="Idle"&&!(m_core->controllerState=="Door"&&m_core->doorSubstate==0)))return;
        f.settingsFingerprint=m_core->setupReadback.fingerprint;f.startupSequence=m_core->startupSequence;f.captureReset=true;f.phase="resetting";f.deadline=localNow()+15000;f.reason="Bewusster Reset mit zuvor bestätigten leeren Startupblöcken; neue Daten abwarten, keine XY-Bewegung.";
        m_core->stop();return;
    }
    if(!m_core->operatorLocalPermit||!m_core->verified()||!m_core->localMotionSafetyError().isEmpty())return;
    if(request.kind==ControllerRuntimeRequestKind::LocalResume) {
        if(f.phase!="paused"||!m_core->localExecutionActive||!m_core->paused||m_core->controllerState!="Hold"||m_core->firing)return;
        if(m_localFrameRun) {
            if(!m_localActive || m_localActive->frameLines().isEmpty() || m_core->localExecutionKind!=GrblSession::LocalExecutionKind::FrozenS0Frame || !m_core->knownOutput || m_core->reportedOutput!=0 || m_accessory.activeLease()) {m_core->stop();return;}
        } else if(!beginAccessory(AccessoryContract::OperatorAction::Resume)){m_core->stop();return;}
        m_operatorPaused=false;m_localMotionStartCounter=m_core->reportedMachinePosition;m_localFirstMotionDue=request.input.proof->observedAt+10000;
        m_core->pause();f.phase="running";return;
    }
    const auto p=request.localPrepared;
    const bool frame=request.kind==ControllerRuntimeRequestKind::LocalFrame;
    if((frame?(f.phase!="ready"&&f.phase!="travelReview"):f.phase!="ready")||!p||!p->valid()||!m_localCandidate||p->fingerprint()!=m_localCandidate->fingerprint()||(!frame&&f.approvedArtifact!=p->fingerprint())||(frame&&p->frameLines().isEmpty())||request.bounds.value("approvalId").toByteArray()!=f.approvalId||!sameLocal(p->headAnchor(),m_core->position)||!m_core->ready())return;
    if(request.kind==ControllerRuntimeRequestKind::LocalStart) {
        if(p->firstNonzeroMotionLineIndex()<0){f.reason="Lokaler Auftrag enthält keinen nichtnulligen Verfahrweg.";return;}
        if(!beginAccessory(AccessoryContract::OperatorAction::StartJob,p->sourceFingerprint()+p->profileFingerprint()))return;
        m_jobActive=true;m_activePrepared=p->prepared();
    }
    m_localActive=p;m_localFrameRun=request.kind==ControllerRuntimeRequestKind::LocalFrame;
    m_localMotionStartCounter=m_core->reportedMachinePosition;m_localFirstMotionDue=request.input.proof->observedAt+10000;
    f.phase="running";f.reason=m_localFrameRun?"Explizite laserfreie lokale Rahmenfahrt mit Rückkehr zum Ausgangspunkt.":"Beaufsichtigter lokaler Auftrag läuft; kein globaler Referenznachweis.";
    bool accepted=m_localFrameRun?m_core->frameLocalPrepared(p->frameLines()):m_core->startLocalPrepared(*p->prepared());
    if(!accepted){m_jobActive=false;m_accessory.transition(AccessoryContract::Event::Complete);retirePrepared();invalidateLocalFrame("Lokaler Start nicht angenommen.");}
}
void ControllerRuntimeWorker::updateLocalFrame() {
    if(m_updatingLocal||!m_local.engaged()||m_local.phase=="invalid")return;
    QScopedValueRollback<bool> guard(m_updatingLocal,true);auto &f=m_local;
    if(!m_core->connected()||f.connectionEpoch!=m_connectionEpoch){invalidateLocalFrame("Verbindung geändert; lokale Lage unbekannt.");return;}
    if((f.phase=="resetting"||f.phase=="capturing"||(f.phase=="reading"&&f.readingAfterReset)) &&
       m_core->statusFresh() && m_core->controllerState=="Door" && m_core->doorSubstate!=0) {
        invalidateLocalFrame("Haube während lokaler Aufnahme geöffnet/ungeprüft; Kopf und Restwege neu bestätigen.");return;
    }
    if(f.phase=="resetting") {
        if(localNow()>f.deadline){invalidateLocalFrame("Lokale Reset-/Readbackvorbereitung überschreitet Frist.");return;}
        if(m_core->awaitingReset||m_core->handshaking)return;
        if(!m_core->statusFresh())return;
        if(m_core->startupSequence!=f.startupSequence+1||m_core->controllerState!="Idle"||!m_core->knownOutput||m_core->reportedOutput!=0||m_core->activeInputs){invalidateLocalFrame("Controller nach Reset nicht frisch Idle/S0; Door/Alarm wird nicht mit Resume/Unlock umgangen.");return;}
        f.startupSequence=m_core->startupSequence;f.readingAfterReset=true;f.phase="reading";f.deadline=localNow()+15000;
        if(!m_core->beginSetupReadback(true))invalidateLocalFrame("Neuer lokaler Controllerreadback konnte nicht gestartet werden.");
        return;
    }
    if(m_core->startupSequence!=f.startupSequence||m_core->awaitingReset||(m_core->handshaking&&!m_core->setupReadback.pending)){invalidateLocalFrame("Neustart/Reset entwertet lokalen Rahmen.");return;}
    if(f.phase=="reading") {
        if(localNow()>f.deadline){invalidateLocalFrame("Vollständiger lokaler Readback fehlt innerhalb der Frist.");return;}
        if(!m_core->setupReadback.complete)return;
        if(!m_core->setupReadback.valid||!m_core->setupReadback.operatorLocal||m_core->setupReadback.settings.value(20)!=0||!m_core->setupReadback.settings.contains(1)){invalidateLocalFrame("Vollständige leere Startups, lokale Modal-/Settingsdaten und $20=0 erforderlich.");return;}
        if(!f.readingAfterReset){f.settingsFingerprint=m_core->setupReadback.fingerprint;f.phase="positioning";f.reason="Nur Daten gelesen. Motorfreigabe tatsächlich prüfen, Kopf von Hand setzen, vier Restwege messen; Haube danach schließen.";return;}
        if(f.settingsFingerprint!=m_core->setupReadback.fingerprint){invalidateLocalFrame("Firmware/Settings haben sich während manueller Aufnahme verändert.");return;}
        if(!m_core->ready()||!m_core->verified())return;
        if(!sameLocal(m_core->offset,m_core->setupReadback.g54)){invalidateLocalFrame("G54 und gemeldeter WCO widersprechen sich vor lokaler Aufnahme.");return;}
        f.phase="capturing";f.deadline=localNow()+6000;
        const QPointF wanted(std::ceil((f.available.negativeX-2)*1000)/1000,std::ceil((f.available.negativeY-2)*1000)/1000);
        if(!m_core->beginLocalCoordinateCapture(wanted))invalidateLocalFrame("Nichtbewegende G54-Aufnahme wurde nicht angenommen.");
        return;
    }
    if((f.phase=="captureReady"||f.phase=="positioning") && m_core->controllerState=="Door" && m_core->doorSubstate!=0) {
        if(f.attested)f.frameId=nonce();
        f.attested=false;f.phase="positioning";f.reason="Haube offen/ungeprüft; nach Handpositionierung schließen und Beobachtungen neu bestätigen.";
    }
    if(!m_core->setupReadback.valid||m_core->setupReadback.fingerprint!=f.settingsFingerprint){invalidateLocalFrame("Lokale Settings/Firmware nicht mehr bestätigt.");return;}
    if(f.phase=="capturing") {
        if(localNow()>f.deadline){invalidateLocalFrame("Lokale G54-Aufnahme nicht fristgerecht bestätigt.");return;}
        if(m_core->workZero)return;
        if(!m_core->localCaptureResult||!m_core->ready()||!m_core->verified())return;
        const auto r=*m_core->localCaptureResult;f.confirmedHead=r.head;f.counterAtCapture=r.counter;f.offsetAtCapture=r.wco;
        GrblSession::OperatorLocalPermit permit{f.frameId,f.settingsFingerprint,r.counter,r.head,r.wco,
            QRectF(QPointF(2-f.available.negativeX,2-f.available.negativeY),QPointF(f.available.positiveX-2,f.available.positiveY-2))};
        m_core->operatorLocalPermit=permit;m_core->positionReferenced=false;
        f.phase="preparing";f.reason="Lokaler G54-Kopf durch ACK und frischen unveränderten Rohzähler bestätigt. CurrentPosition-Auftrag im Hintergrund vorbereiten.";return;
    }
    if(m_core->operatorLocalPermit) {
        if(!m_core->localMotionSafetyError().isEmpty()){invalidateLocalFrame(m_core->localMotionSafetyError());return;}
        if(m_core->localExecutionActive) {
            if(!sameLocal(m_core->reportedMachinePosition,m_localMotionStartCounter))m_localFirstMotionDue=-1;
            if(m_localFirstMotionDue>=0&&localNow()>m_localFirstMotionDue){invalidateLocalFrame("Innerhalb 10 s kein gemeldeter nichtnulliger lokaler Bewegungsbeginn; Auftrag verworfen.");return;}
            if(m_core->paused){f.phase="paused";m_localFirstMotionDue=-1;}
            if(!m_core->busy()&&!m_core->firing) {
                m_core->localExecutionActive=false;m_core->localExecutionKind=GrblSession::LocalExecutionKind::None;m_localFirstMotionDue=-1;retireLocalJob(std::move(m_localActive));
                if(m_localFrameRun){f.phase=(m_localCandidate && f.approvedArtifact==m_localCandidate->fingerprint())?"ready":"travelReview";f.reason="Lokale laserfreie Rahmenfahrt beendet; frischer Startintent erforderlich.";}
                else {f.approvedArtifact.clear();f.approvalId.clear();f.phase="preparing";f.reason="Auftrag beendet; nächsten aktuellen lokalen Plan erneut prüfen und bestätigen.";}
            }
        }
    } else if(f.phase!="positioning"&&f.phase!="captureReady")invalidateLocalFrame("Lokale Aufnahme verlorengegangen.");
}
