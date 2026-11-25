#include "controllerruntimeworker.h"
#include "controllerruntimemailbox.h"
#include "grblwirenumbers.h"
#include <QScopedValueRollback>
#include <QDataStream>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <QRegularExpression>
namespace {
qint64 setupNow(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
bool closePoint(QPointF a,QPointF b){return std::abs(a.x()-b.x())<=0.002 && std::abs(a.y()-b.y())<=0.002;}
bool finiteSetup(double x){return std::isfinite(x);}
}
void ControllerRuntimeWorker::discardConnectionHomePermit() {
    m_connectionHomePermit.reset();m_connectionHomeEpoch=m_connectionHomeRequestId=0;m_connectionHomeEndpoint.clear();
    // Keep the started marker until $H is written or its queue is cancelled.
    // Losing a permit while $H waits behind M5 must never turn into an ordinary Home.
}
bool ControllerRuntimeWorker::acceptConnectionHomePermit(const ControllerRuntimeRequest &request) {
    const auto &p=request.connectionHomePermit;const auto now=setupNow();
    if(!p || p->issuer!=m_mailbox->inputIssuer || p->requestId!=request.id ||
       p->generation!=request.generation || p->generation!=m_mailbox->generation.load() ||
       p->preOpenEpoch!=request.connectionEpoch || p->preOpenEpoch+1!=m_connectionEpoch ||
       p->endpoint!=request.endpoint || p->binding!=request.binding ||
       p->profileFingerprint!=m_profile.outputFingerprint() ||
       p->recordDigest!=ControllerConnectionHomeDigest::record(request.calibrationRecord) ||
       p->contextDigest!=ControllerConnectionHomeDigest::context(request.endpoint,request.binding,m_profile.outputFingerprint()) ||
       !p->interaction || p->observedAt<=0 || p->admittedAt<p->observedAt ||
       p->admittedAt-p->observedAt>250 || p->deadline-p->observedAt!=15000 ||
       now<p->admittedAt || now>p->deadline || m_mailbox->stop.load() || m_mailbox->close.load() || m_mailbox->shutdown.load())return false;
    m_connectionHomePermit=p;m_connectionHomeEpoch=m_connectionEpoch;m_connectionHomeRequestId=request.id;
    m_connectionHomeEndpoint=request.endpoint;m_connectionHomeStarted=false;
    return true;
}
bool ControllerRuntimeWorker::validateConnectionHomePermitAtWrite() {
    if(!m_connectionHomeStarted)return true; // Ordinary genuine native Home keeps its existing contract.
    if(!checkClockContinuity())return false;
    const auto &p=m_connectionHomePermit;const auto now=setupNow();const auto &r=m_core->setupReadback;
    if(!p || now<p->admittedAt || now>p->deadline || m_connectionHomeEpoch!=m_connectionEpoch ||
       p->generation!=m_mailbox->generation.load() || p->issuer!=m_mailbox->inputIssuer ||
       p->preOpenEpoch+1!=m_connectionEpoch || p->requestId!=m_connectionHomeRequestId ||
       p->endpoint!=m_connectionHomeEndpoint ||
       p->contextDigest!=ControllerConnectionHomeDigest::context(m_connectionHomeEndpoint,m_calibrationBinding,m_profile.outputFingerprint()) ||
       m_calibrationBinding!=p->binding || m_profile.outputFingerprint()!=p->profileFingerprint ||
       ControllerConnectionHomeDigest::record(m_calibrationCandidate)!=p->recordDigest ||
       !r.valid || r.fingerprint!=m_commissioning.fingerprint ||
       !m_commissioning.restoring || m_commissioning.stage!="restoringHome" ||
       !m_core->connected() || m_core->awaitingReset || m_core->handshaking || m_core->paused ||
       !m_core->statusFresh() || !m_core->configurationValid() ||
       !m_core->knownReportedMachinePosition || !m_core->knownOffset || !m_core->knownOutput ||
       m_core->reportedOutput!=0 || m_core->activeInputs || m_core->offset!=r.g54 ||
       (m_core->controllerState!="Idle"&&m_core->controllerState!="Alarm") ||
       m_accessory.activeLease() || m_jobActive || m_focusActive || m_core->firing ||
       m_mailbox->stop.load() || m_mailbox->close.load() || m_mailbox->shutdown.load())return false;
    return true;
}
void ControllerRuntimeWorker::startCalibratedHome() {
    auto &c=m_commissioning;
    m_core->commissionedHomingContract=true;
    c.stage=c.restoring?"restoringHome":"homing";c.deadline=setupNow()+90000;
    c.reason=c.restoring?"Gespeicherte Kalibrierung passt; die bewusst autorisierte Referenzfahrt läuft.":"Referenzfahrt läuft; tatsächlichen Homeort anschließend bestätigen.";
    m_core->home();m_core->commissionedHomingContract=false;
    if(!m_core->homingPending)cancelCommissioning("Referenzfahrt wurde nicht angenommen.");
}

void ControllerRuntimeWorker::clearCalibrationSession() {
    discardAttendedStart("Verbindung oder Kalibrierung beendet.");
    m_attestedWorkOffsetKnown=false;
    discardConnectionHomePermit();m_connectionHomeStarted=false;
    m_calibrationReadbackPending=false;
    m_calibrationCandidate.clear();m_calibrationBinding.clear();
}
void ControllerRuntimeWorker::beginCalibrationRestore(const QVariantMap &record,const QByteArray &binding) {
    clearCalibrationSession();
    m_commissioning={};m_commissioning.connectionEpoch=m_connectionEpoch;
    m_calibrationBinding=binding;m_calibrationCandidate=record;
    m_restoreReason.clear();m_calibrationReadbackPending=true;
    m_commissioning.stage="readback";
    m_commissioning.reason="Verbindung initialisieren; vollständige Controllerdaten werden automatisch gelesen.";
}
bool ControllerRuntimeWorker::matchCalibrationCandidate() {
    const auto &r=m_calibrationCandidate;auto &c=m_commissioning;
    const auto reject=[&](QString reason){m_restoreReason=std::move(reason);return false;};
    if(r.isEmpty())return reject("Keine passende gespeicherte Kalibrierung; einmalige vollständige Einrichtung erforderlich.");
    const QStringList hashes={"binding","deviceIdentityHash","profileFingerprint","settingsFingerprint"};
    const QStringList numbers={"homeX","homeY","minX","minY","maxX","maxY","observedMinX","observedMinY","observedMaxX","observedMaxY","workOriginX","workOriginY"};
    QSet<QString> allowed{"schema"};for(const auto &key:hashes)allowed.insert(key);for(const auto &key:numbers)allowed.insert(key);
    if(r.size()!=allowed.size())return reject("Gespeicherte Kalibrierdaten haben ein unbekanntes Format.");
    for(auto i=r.cbegin();i!=r.cend();++i)if(!allowed.contains(i.key()))return reject("Unbekanntes Kalibrierfeld.");
    const auto numeric=[&](const QString &key,double &value){
        const auto v=r.value(key);const auto type=v.metaType().id();
        if(type!=QMetaType::Double&&type!=QMetaType::Int&&type!=QMetaType::LongLong&&type!=QMetaType::UInt&&type!=QMetaType::ULongLong)return false;
        value=v.toDouble();return std::isfinite(value)&&std::abs(value)<=10000;
    };
    double schema=0;if(!numeric("schema",schema)||schema!=1)return reject("Kalibrierformat ist veraltet oder ungültig.");
    static const QRegularExpression hex(QStringLiteral("^[0-9a-f]{64}$"));
    for(const auto &key:hashes)if(r.value(key).metaType().id()!=QMetaType::QString||!hex.match(r.value(key).toString()).hasMatch())return reject("Kalibrierbindung ist ungültig.");
    for(const auto &key:numbers){double value;if(!numeric(key,value))return reject("Kalibriergeometrie enthält ungültige Zahlen.");}
    const auto value=[&](const QString &key){return r.value(key).toDouble();};
    if(r.value("binding").toString().toLatin1()!=m_calibrationBinding ||
       r.value("profileFingerprint").toString().toLatin1()!=m_profile.outputFingerprint())return reject("Geräteinstanz oder Profil unterscheidet sich von der gespeicherten Kalibrierung.");
    const auto &live=m_core->setupReadback;
    if(live.deviceIdentityHash.size()!=32 || r.value("deviceIdentityHash").toString().toLatin1()!=live.deviceIdentityHash.toHex())return reject("Frische eindeutige Controlleridentität fehlt oder ist verändert.");
    if(r.value("settingsFingerprint").toString().toLatin1()!=live.fingerprint.toHex())return reject("Firmware oder kalibrierrelevante Controllerdaten haben sich verändert.");
    const QPointF home(value("homeX"),value("homeY"));
    const QPointF low(value("observedMinX"),value("observedMinY")),high(value("observedMaxX"),value("observedMaxY"));
    const double minX=value("minX"),minY=value("minY"),maxX=value("maxX"),maxY=value("maxY");
    if(minX>=maxX||minY>=maxY||low.x()>home.x()||low.y()>home.y()||high.x()<home.x()||high.y()<home.y())return reject("Gespeicherter Referenz-/Innenraum ist widersprüchlich.");
    const QRectF approach(QPointF(std::min(minX,low.x()),std::min(minY,low.y())),QPointF(std::max(maxX,high.x()),std::max(maxY,high.y())));
    if(approach.width()>m_profile.workArea().width()+.001||approach.height()>m_profile.workArea().height()+.001)return reject("Gespeicherter Anfahrtsraum überschreitet den Profilfahrweg.");
    c.home=home;c.observedMin=low;c.observedMax=high;c.limits=QRectF(QPointF(minX,minY),QPointF(maxX,maxY));
    // Current G54 must be usable on the actual decimal wire grid. Never reuse
    // old work coordinates or silently restore G10 offsets.
    for(const auto point:{c.limits.topLeft(),c.limits.topRight(),c.limits.bottomLeft(),c.limits.bottomRight()}){
        const auto work=point-m_core->offset;
        const QPointF actual(GrblWireNumbers::coordinate(work.x())+m_core->offset.x(),GrblWireNumbers::coordinate(work.y())+m_core->offset.y());
        if(actual.x()<minX||actual.x()>maxX||actual.y()<minY||actual.y()>maxY)return reject("Aktueller G54-Versatz passt nicht zum gespeicherten Wire-Innenraum; neue Zuordnung erforderlich.");
    }
    c.restoring=true;m_restoreReason="Gespeicherte Kalibrierung stimmt mit den frisch gelesenen Controllerdaten überein.";
    return true;
}
void ControllerRuntimeWorker::finishCalibrationReference() {
    auto &c=m_commissioning;
    if(!m_core->insideReportedMachineBed()){cancelCommissioning("Frischer Maschinenpunkt liegt nicht im gespeicherten Innenraum.");return;}
    if(m_attendedStart) {
        if(m_attendedStart->referenceOnly && !(m_attestedWorkOffsetKnown && m_attestedWorkOffset==m_core->offset) && !m_calibrationCandidate.isEmpty() &&
           QPointF(m_calibrationCandidate.value("workOriginX").toDouble(),m_calibrationCandidate.value("workOriginY").toDouble())!=m_core->offset) {
            c.stage="restoredOriginReview";c.reason="Referenz bestätigt. Geänderten G54-Arbeitsursprung vor Bearbeitung ausdrücklich prüfen.";return;
        }
        m_attestedWorkOffset=m_core->offset;m_attestedWorkOffsetKnown=true;
        c.stage="complete";c.reason="Startreferenz und unveränderter G54 frisch bestätigt.";return;
    }
    const QPointF origin=m_attestedWorkOffsetKnown?m_attestedWorkOffset:
        QPointF(m_calibrationCandidate.value("workOriginX").toDouble(),m_calibrationCandidate.value("workOriginY").toDouble());
    c.restored=true;
    if(origin==m_core->offset) {
        m_attestedWorkOffset=m_core->offset;m_attestedWorkOffsetKnown=true;
        c.stage="complete";c.reason="Gespeicherte Kalibrierung und aktueller G54 bestätigt; Maschinenreferenz frisch erneuert.";
    } else {
        c.stage="restoredOriginReview";c.reason="Maschinenraum und Referenz bestätigt. Aktueller G54-Arbeitsursprung hat sich verändert; die angezeigte Zuordnung ausdrücklich bestätigen.";
    }
}
void ControllerRuntimeWorker::publishCalibrationRecord() {
    const auto &c=m_commissioning;const auto &live=m_core->setupReadback;
    if(c.stage!="complete")return;
    m_attestedWorkOffset=m_core->offset;m_attestedWorkOffsetKnown=true;
    if(live.deviceIdentityHash.size()!=32||m_calibrationBinding.size()!=64) {
        m_commissioning.reason="Einrichtung gilt nur für diese Verbindung; eindeutige Controllerkennung oder Gerätebindung fehlt, Speichern nicht möglich.";
        return;
    }
    QVariantMap r{{"schema",1},{"binding",QString::fromLatin1(m_calibrationBinding)},
        {"deviceIdentityHash",QString::fromLatin1(live.deviceIdentityHash.toHex())},
        {"profileFingerprint",QString::fromLatin1(m_profile.outputFingerprint())},
        {"settingsFingerprint",QString::fromLatin1(live.fingerprint.toHex())},
        {"homeX",c.home.x()},{"homeY",c.home.y()},
        {"minX",c.limits.left()},{"minY",c.limits.top()},{"maxX",c.limits.right()},{"maxY",c.limits.bottom()},
        {"observedMinX",c.observedMin.x()},{"observedMinY",c.observedMin.y()},{"observedMaxX",c.observedMax.x()},{"observedMaxY",c.observedMax.y()},
        {"workOriginX",m_core->offset.x()},{"workOriginY",m_core->offset.y()}};
    emit calibrationCompleted(m_calibrationBinding,r);
}

void ControllerRuntimeWorker::cancelCommissioning(const QString &reason) {
    discardAttendedStart(reason);
    discardConnectionHomePermit();
    m_calibrationReadbackPending=false; // STOP/cancel cannot restart the connect readback later.
    m_calibrationCandidate.clear();
    if(m_commissioning.stage=="inactive" || m_commissioning.stage=="fault")return;
    m_commissioning.stage="fault";m_commissioning.reason=reason;
    m_core->commissionedEnvelope.reset();
    m_core->commissionedModalContract=m_core->commissionedHomingContract=false;
    m_core->positionReferenced=false;
    if(m_core->connected() && m_core->busy() && !m_core->awaitingReset)m_core->stop();
}
QVariantMap ControllerRuntimeWorker::commissioningSnapshot() const {
    const auto &c=m_commissioning;QVariantMap settings;
    for(auto i=m_core->setupReadback.settings.cbegin();i!=m_core->setupReadback.settings.cend();++i)settings[QString::number(i.key())]=i.value();
    QVariantMap result{{"stage",c.stage},{"reason",c.reason},{"active",c.active()},
        {"controllerState",m_core->controllerState},{"activeInputs",m_core->activeInputs},
        {"restoreReason",m_restoreReason},{"hasSavedCalibration",c.restoring||c.restored},
        {"calibrationSaveAvailable",m_core->setupReadback.deviceIdentityHash.size()==32&&m_calibrationBinding.size()==64},
        {"ready",c.stage=="complete"},{"settings",settings},{"fingerprint",QString::fromLatin1(c.fingerprint.toHex())},
        {"machineX",m_core->reportedMachinePosition.x()},{"machineY",m_core->reportedMachinePosition.y()},
        {"positionKnown",m_core->knownReportedMachinePosition && m_core->statusFresh()},
        {"workX",m_core->position.x()},{"workY",m_core->position.y()},
        {"wcoX",m_core->offset.x()},{"wcoY",m_core->offset.y()},
        {"minX",c.limits.left()},{"minY",c.limits.top()},{"maxX",c.limits.right()},{"maxY",c.limits.bottom()},
        {"workMinX",c.limits.left()-m_core->offset.x()},{"workMinY",c.limits.top()-m_core->offset.y()},
        {"workMaxX",c.limits.right()-m_core->offset.x()},{"workMaxY",c.limits.bottom()-m_core->offset.y()},
        {"targetMachineX",c.target.x()},{"targetMachineY",c.target.y()},{"cornerIndex",c.cornerIndex},{"cornersConfirmed",c.cornersConfirmed},
        {"probesConfirmed",c.probesConfirmed},{"homeObserved",c.homeObserved}};
    return result;
}
void ControllerRuntimeWorker::setupData(const QVariantMap &data) {
    const auto op=data.value("op").toString();auto &c=m_commissioning;
    if(op=="cancel"){cancelCommissioning("Einrichtung abgebrochen; neue Referenz erforderlich.");return;}
    const auto denied=[&](const QString &reason){c.reason=reason;};
    if(m_accessory.activeLease() || m_jobActive || m_focusActive || m_core->firing || m_core->paused){denied("Einrichtung gesperrt: aktive Ausgabe oder Zubehörfreigabe.");return;}
    if(op=="begin") {
        // Connect already performs this readback. A delayed Begin from its
        // previous GUI snapshot must not restart a completed data transaction
        // and race the operator's queued mechanics attestation.
        if(c.stage=="readback" ||
           (c.stage=="mechanics" && c.connectionEpoch==m_connectionEpoch &&
            m_core->setupReadback.valid && c.fingerprint==m_core->setupReadback.fingerprint))return;
        // Begin from savedHomeReady remains an explicit full recalibration.
        if(!m_core->setupReadyForAttestation()){denied("Frischer geprüfter Idle- oder Homing-Alarmstatus mit S0 für Einrichtung erforderlich.");return;}
        discardConnectionHomePermit();
        m_calibrationCandidate.clear();m_restoreReason.clear();
        c={};c.connectionEpoch=m_connectionEpoch;c.stage="readback";c.reason="Lese Einstellungen, Modalzustand, Offsets und Startupblöcke.";c.deadline=setupNow()+15000;
        if(!m_core->beginSetupReadback())cancelCommissioning("Controllerdaten konnten nicht angefordert werden.");
        return;
    }
    if(!c.active() || c.stage=="complete" || !m_core->setupReadback.valid ||
       c.fingerprint!=m_core->setupReadback.fingerprint){denied("Aktuelle gebundene Controllerdaten fehlen.");return;}
    if(!m_core->setupReadyForAttestation()){denied("Aktion erst nach frischem Idle/S0 oder geprüftem Homing-Alarm möglich.");return;}
    if(op=="mechanics" && c.stage=="mechanics") {
        for(const auto &key:{"switches","direction","freeTravel","laserDisabled"})
            if(data.value(key).metaType().id()!=QMetaType::Bool || !data.value(key).toBool()) {denied("Endschalter, Richtung, freier Fahrweg einschließlich ±2-mm-Probenraum und deaktivierte Laseremission einzeln bestätigen.");return;}
        if(m_core->setupReadback.settings.value(22)!=1 || m_core->setupReadback.settings.value(24)<=0 || m_core->setupReadback.settings.value(25)<=0 || m_core->setupReadback.settings.value(27)<=0){denied("Homingparameter nicht verwendbar.");return;}
        c.stage="homeReady";c.reason="Beobachtete Referenzfahrt ausdrücklich starten; STOPP bleibt jederzeit möglich.";
    } else if(op=="homeObserved" && c.stage=="geometry" && !c.homeObserved) {
        if(!m_core->positionReferenced || !m_core->knownReportedMachinePosition){denied("Referenzreadback fehlt.");return;}
        c.homeObserved=true;c.home=m_core->reportedMachinePosition;
        c.observedMin=c.observedMax=c.home;
        m_core->commissionedEnvelope=QRectF(c.home-QPointF(2,2),QSizeF(4,4));
        m_core->commissionedModalContract=true;
        c.reason="Tatsächlichen Homeort bestätigt. Kleine X- und Y-Probe (höchstens 1 mm) beobachten und vermessene Strecke/Richtung bestätigen.";
    } else if(op=="probeObserved" && c.stage=="probeConfirm") {
        if(!closePoint(m_core->reportedMachinePosition,c.target)){cancelCommissioning("Probenposition hat sich verändert.");return;}
        const auto observed=m_core->reportedMachinePosition;
        c.observedMin={std::min({c.observedMin.x(),observed.x(),c.target.x()}),std::min({c.observedMin.y(),observed.y(),c.target.y()})};
        c.observedMax={std::max({c.observedMax.x(),observed.x(),c.target.x()}),std::max({c.observedMax.y(),observed.y(),c.target.y()})};
        ++c.probesConfirmed;c.stage=c.xConfirmed&&c.yConfirmed?"limits":"geometry";
        c.reason=c.xConfirmed&&c.yConfirmed?"X/Y geprüft. Gemessenen konservativen Innenfahrraum in Maschinenkoordinaten eingeben.":"Zweite Achse mit kleiner Probe prüfen und Strecke/Richtung bestätigen.";
    } else if(op=="limits" && c.stage=="limits" && c.homeObserved && c.xConfirmed && c.yConfirmed) {
        const double minX=data.value("minX").toDouble(),minY=data.value("minY").toDouble(),maxX=data.value("maxX").toDouble(),maxY=data.value("maxY").toDouble();
        for(const auto &key:{"minX","minY","maxX","maxY"}) {
            const auto type=data.value(key).metaType().id();
            if(type!=QMetaType::Double && type!=QMetaType::Int){denied("Vier endliche gemessene Zahlen erforderlich.");return;}
        }
        if(!finiteSetup(minX)||!finiteSetup(minY)||!finiteSetup(maxX)||!finiteSetup(maxY)||minX>=maxX||minY>=maxY ||
           maxX-minX>m_profile.workArea().width()+0.001 || maxY-minY>m_profile.workArea().height()+0.001 ||
           std::abs(minX)>10000||std::abs(minY)>10000||std::abs(maxX)>10000||std::abs(maxY)>10000) {denied("Innenraum muss endlich, positiv groß und höchstens nominal 200×150 mm sein; Vorzeichen werden nicht vorgegeben.");return;}
        const QRectF limits(QPointF(minX,minY),QPointF(maxX,maxY));
        // The operator attests the complete rectangle including the approach from
        // observed home. A home outside the interior is allowed; shrink only once
        // the head is inside after all four corner probes.
        // The +/-2 mm box authorizes only the explicitly attested probe phase.
        // It is not evidence that unvisited space on both sides of Home belongs
        // to the machine travel. Keep every confirmed probe endpoint, including
        // any outside the requested interior, when checking the approach span.
        const QRectF approach(QPointF(std::min(minX,c.observedMin.x()),std::min(minY,c.observedMin.y())),
                              QPointF(std::max(maxX,c.observedMax.x()),std::max(maxY,c.observedMax.y())));
        if(approach.width()>m_profile.workArea().width()+.001 || approach.height()>m_profile.workArea().height()+.001) {
            denied("Innenraum samt bestätigtem Home-/Probenraum überschreitet den gemeldeten Fahrweg; Maschinenvorzeichen und Messwerte prüfen.");return;
        }
        for(const auto point:{limits.topLeft(),limits.topRight(),limits.bottomLeft(),limits.bottomRight()}) {
            const auto work=point-m_core->offset;
            const QPointF actual(GrblWireNumbers::coordinate(work.x())+m_core->offset.x(),GrblWireNumbers::coordinate(work.y())+m_core->offset.y());
            if(actual.x()<limits.left()||actual.x()>limits.right()||actual.y()<limits.top()||actual.y()>limits.bottom()) {
                denied("Ecken liegen nach tatsächlicher 0,001-mm-GCode-Formatierung außerhalb des Innenraums. Grenzen passend zum WCO mit Sicherheitsabstand wählen.");return;
            }
        }
        c.limits=limits;
        m_core->commissionedEnvelope=approach;
        c.cornerIndex=c.cornersConfirmed=0;c.target=c.limits.bottomLeft();c.stage="cornerReady";
        c.reason="Gemessenen freien Anfahrtsraum von Home und Sicherheitsabstand bestätigen; Ecke 1 laserfrei anfahren.";
    } else if(op=="cornerObserved" && c.stage=="cornerConfirm") {
        if(!closePoint(m_core->reportedMachinePosition,c.target)){cancelCommissioning("Eckenposition hat sich verändert.");return;}
        ++c.cornersConfirmed;++c.cornerIndex;
        if(c.cornersConfirmed==4) {
            c.stage="workCoordinates";m_core->commissionedEnvelope=c.limits;
            c.reason="Vier physische Ecken bestätigt. G54/WPos/WCO und Modellzuordnung prüfen; derzeitigen Arbeitsnullpunkt ausdrücklich übernehmen.";
        } else {c.target=c.cornerIndex==1?c.limits.bottomRight():c.cornerIndex==2?c.limits.topRight():c.limits.topLeft();c.stage="cornerReady";c.reason="Physische Ecke bestätigt; nächste Ecke laserfrei anfahren.";}
    } else if(op=="coordinatesObserved" && (c.stage=="workCoordinates"||c.stage=="restoredOriginReview")) {
        if(!m_core->positionReferenced || !m_core->insideReportedMachineBed() || !m_core->knownOffset){denied("Aktueller G54-/Positionsreadback fehlt.");return;}
        c.stage="complete";c.reason="Gemessener Fahrraum und G54-Zuordnung bestätigt. Gerätebetrieb und Material vor dem Start prüfen.";
        publishCalibrationRecord();
    } else denied("Dieser Einrichtungsschritt ist in der aktuellen Phase nicht zulässig.");
}
void ControllerRuntimeWorker::setupMotion(const ControllerRuntimeRequest &request) {
    auto &c=m_commissioning;
    if(m_accessory.activeLease()||m_jobActive||m_focusActive||m_core->firing||m_core->paused||m_core->busy()||!m_core->statusFresh()||
       !m_core->setupReadback.valid || c.fingerprint!=m_core->setupReadback.fingerprint)return;
    if(request.kind==ControllerRuntimeRequestKind::SetupHome && (c.stage=="homeReady"||c.stage=="savedHomeReady")) {
        discardConnectionHomePermit();m_connectionHomeStarted=false;
        startCalibratedHome();
        return;
    }
    if(!m_core->ready()||!m_core->verified()||!m_core->positionReferenced||!c.homeObserved)return;
    if(request.kind==ControllerRuntimeRequestKind::SetupZero && c.stage=="workCoordinates") {
        c.stage="zeroPending";c.deadline=setupNow()+6000;c.reason="Arbeitsnullpunkt: ACKs und frischen unveränderten Maschinenpunkt prüfen.";
        m_core->zero();if(!m_core->workZero)cancelCommissioning("Arbeitsnullpunkt wurde nicht angenommen.");return;
    }
    QPointF target;
    if(request.kind==ControllerRuntimeRequestKind::SetupProbe && c.stage=="geometry") {
        const double dx=request.x,dy=request.y;
        if(!finiteSetup(dx)||!finiteSetup(dy)||(dx==0)==(dy==0)||std::abs(dx)>1||std::abs(dy)>1)return;
        target=m_core->reportedMachinePosition+QPointF(dx,dy);
        if(std::abs(target.x()-c.home.x())>2 || std::abs(target.y()-c.home.y())>2)return;
        if(dx!=0)c.xConfirmed=true;
        if(dy!=0)c.yConfirmed=true;
        c.stage="probePending";
    } else if(request.kind==ControllerRuntimeRequestKind::SetupCorner && c.stage=="savedApproachReady") {
        if(request.x!=0)return;
        target=c.limits.topLeft();c.stage="restoringApproach";
    } else if(request.kind==ControllerRuntimeRequestKind::SetupCorner && c.stage=="cornerReady") {
        if(request.x!=c.cornerIndex||c.cornerIndex<0||c.cornerIndex>3)return;
        const auto r=c.limits;target=c.cornerIndex==0?r.bottomLeft():c.cornerIndex==1?r.bottomRight():c.cornerIndex==2?r.topRight():r.topLeft();
        c.stage="cornerPending";
    } else return;
    const QPointF work=target-m_core->offset;
    if(!m_core->insideMachineBed(work)){cancelCommissioning("Gerundetes Fahrziel liegt außerhalb des bestätigten Anfahrtsraums.");return;}
    c.target=target;c.deadline=setupNow()+90000;c.reason="Laserfreie langsame Einzelbewegung läuft; physische Beobachtung anschließend bestätigen.";
    const auto x=GrblWireNumbers::format(work.x()),y=GrblWireNumbers::format(work.y());
    if(!m_core->commands({"M5 S0","G21 G90 G54",QString("G1 X%1 Y%2 F300 S0").arg(x,y)}))cancelCommissioning("Einzelbewegung wurde nicht angenommen.");
}
void ControllerRuntimeWorker::updateCommissioning() {
    if(m_setupUpdating)return;
    if(m_connectionHomePermit && setupNow()>m_connectionHomePermit->deadline) {
        discardConnectionHomePermit();
        m_restoreReason="Automatische Referenzfahrt verworfen: 15-Sekunden-Verbindungsfrist abgelaufen. Referenzfahrt erneut bewusst starten.";
        if(m_connectionHomeStarted){cancelCommissioning(m_restoreReason);return;}
    }
    if(m_calibrationReadbackPending) {
        if(!m_core->connected()||m_core->awaitingReset||m_core->handshaking||m_core->busy()||m_local.engaged())return;
        if(!m_core->setupReadyForAttestation()) {
            m_commissioning.reason="Automatische Datenprüfung wartet auf frischen ruhenden S0-Zustand oder zulässigen Homing-Alarm ohne Sperreingang; keine automatische Bewegung.";
            return;
        }
        m_calibrationReadbackPending=false;
        m_commissioning.deadline=setupNow()+15000;
        if(!m_core->beginSetupReadback())cancelCommissioning("Automatische Controllerdaten konnten nicht angefordert werden.");
        return;
    }
    if(!m_commissioning.active())return;
    QScopedValueRollback<bool> guard(m_setupUpdating,true);auto &c=m_commissioning;
    if(c.connectionEpoch!=m_connectionEpoch || !m_core->connected() || m_core->awaitingReset || (m_core->handshaking && !m_core->setupReadback.pending)) {cancelCommissioning("Verbindung/Reset/Neustart hat die Einrichtung entwertet.");return;}
    if(c.stage!="complete" && c.stage!="readback" && m_core->knownOutput && m_core->reportedOutput!=0) {cancelCommissioning("Unerwartete gemeldete Leistung während laserfreier Einrichtung.");return;}
    if(c.stage=="readback") {
        if(setupNow()>c.deadline){cancelCommissioning("Controllerdaten nicht vollständig innerhalb der Frist.");return;}
        if(m_core->setupReadback.complete) {
            if(!m_core->setupReadback.valid){cancelCommissioning(m_core->setupReadback.error);return;}
            if(m_core->setupReadyForAttestation()) {
                if(!closePoint(m_core->offset,m_core->setupReadback.g54)){cancelCommissioning("G54-Readback und aktueller WCO widersprechen sich.");return;}
                c.fingerprint=m_core->setupReadback.fingerprint;
                if(matchCalibrationCandidate()) {
                    c.stage="savedHomeReady";c.reason="Kalibrierung passt. Bewusste Referenzfahrt starten; ohne passenden Verbindungsklick erfolgt keine Bewegung.";
                    if(m_connectionHomePermit) {
                        m_connectionHomeStarted=true;
                        // At-write validation expects the exact homing transaction phase.
                        c.stage="restoringHome";
                        if(validateConnectionHomePermitAtWrite())startCalibratedHome();
                        else {discardConnectionHomePermit();m_connectionHomeStarted=false;c.stage="savedHomeReady";
                            c.reason="Verbindungsautorisierung nicht mehr gültig; Referenzfahrt erneut bewusst starten.";}
                    }
                }
                else {discardConnectionHomePermit();c.stage="mechanics";c.reason="Controllerdaten frisch gelesen. "+m_restoreReason+" Endschalter, Richtung, freie Mechanik und deaktivierte Laseremission prüfen.";}}
        }
        return;
    }
    if(!m_core->setupReadback.valid || c.fingerprint!=m_core->setupReadback.fingerprint){cancelCommissioning("Controllerkonfiguration verändert oder nicht mehr belegt.");return;}
    if(c.stage=="zeroPending") {
        if(setupNow()>c.deadline){cancelCommissioning("Arbeitsnullpunkt nicht innerhalb der Frist bestätigt.");return;}
        if(m_core->workZero)return;
        if(!m_core->ready()||!m_core->verified()||!m_core->positionReferenced){cancelCommissioning("Arbeitsnullpunkt fehlgeschlagen.");return;}
        c.stage="workCoordinates";c.reason="Arbeitsnullpunkt durch ACKs und frischen unveränderten Maschinenpunkt bestätigt; G54-Modellzuordnung bestätigen.";
    }
    if(c.moving()) {
        if(setupNow()>c.deadline){cancelCommissioning("Einrichtungsbewegung ohne vollständige Bestätigung innerhalb der Frist.");return;}
        if(m_core->busy())return;
        // Grbl's completion log emits changed() before the same status parser
        // latches connectionVerified (notably Alarm -> first successful Home).
        // This intermediate callback cannot finish or fail the transaction.
        if(m_core->ready() && m_core->positionReferenced && !m_core->verified())return;
        if(!m_core->ready() || !m_core->verified() || !m_core->positionReferenced){cancelCommissioning("Frischer Idle/S0-Referenzreadback fehlt.");return;}
        if(c.stage=="restoringHome") {
            if(!closePoint(m_core->reportedMachinePosition,c.home)||!closePoint(m_core->offset,m_attendedStart?m_attendedStart->offset:m_core->setupReadback.g54)){cancelCommissioning("Frische Referenz stimmt nicht mit gespeichertem Home/G54 überein; erneut kalibrieren.");return;}
            c.homeObserved=true;m_core->commissionedModalContract=true;
            const auto current=m_core->reportedMachinePosition;
            const bool inside=current.x()>=c.limits.left()&&current.x()<=c.limits.right()&&current.y()>=c.limits.top()&&current.y()<=c.limits.bottom();
            if(inside) {m_core->commissionedEnvelope=c.limits;finishCalibrationReference();}
            else {
                const QRectF approach(QPointF(std::min(c.limits.left(),c.observedMin.x()),std::min(c.limits.top(),c.observedMin.y())),QPointF(std::max(c.limits.right(),c.observedMax.x()),std::max(c.limits.bottom(),c.observedMax.y())));
                m_core->commissionedEnvelope=approach;c.cornerIndex=0;c.target=c.limits.topLeft();c.stage="savedApproachReady";
                c.reason="Home liegt außerhalb des gespeicherten Innenraums. Bestätigten langsamen S0-Anlauf zum Innenraumursprung ausdrücklich starten.";
            }
        } else if(c.stage=="restoringApproach") {
            if(!closePoint(m_core->reportedMachinePosition,c.target)){cancelCommissioning("Gespeicherter Anlauf hat den bestätigten Innenraumursprung nicht erreicht.");return;}
            m_core->commissionedEnvelope=c.limits;finishCalibrationReference();
        } else if(c.stage=="homing") {
            if(!closePoint(m_core->offset,m_core->setupReadback.g54)){cancelCommissioning("Unerwartete WCO-Änderung während Referenzfahrt.");return;}
            c.home=m_core->reportedMachinePosition;c.stage="geometry";c.reason="Referenz protokolliert. Tatsächlichen Homeort und Pull-off beobachten und bestätigen.";}
        else {
            if(!closePoint(m_core->reportedMachinePosition,c.target)){cancelCommissioning("Einrichtungsziel und gemeldeter physischer Punkt widersprechen sich.");return;}
            c.stage=c.stage=="probePending"?"probeConfirm":"cornerConfirm";
            c.reason="Controller meldet Ziel erreicht/Idle/S0. Tatsächliche Strecke, Richtung und freien Abstand bestätigen.";
        }
    } else if(c.homeObserved && (!m_core->positionReferenced || !m_core->commissionedModalContract))cancelCommissioning("Maschinenreferenz oder Modalvertrag verloren.");
}
