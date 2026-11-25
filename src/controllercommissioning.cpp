#include "controllerruntimeworker.h"
#include "controllerruntimemailbox.h"
#include "grblwirenumbers.h"
#include <QScopedValueRollback>
#include <QDataStream>
#include <chrono>
#include <cmath>
#include <algorithm>
namespace {
qint64 setupNow(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
bool closePoint(QPointF a,QPointF b){return std::abs(a.x()-b.x())<=0.002 && std::abs(a.y()-b.y())<=0.002;}
bool finiteSetup(double x){return std::isfinite(x);}
}
void ControllerRuntimeWorker::cancelCommissioning(const QString &reason) {
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
        if(!m_core->setupReadyForAttestation()){denied("Frischer geprüfter Idle- oder Homing-Alarmstatus mit S0 für Einrichtung erforderlich.");return;}
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
    } else if(op=="coordinatesObserved" && c.stage=="workCoordinates") {
        if(!m_core->positionReferenced || !m_core->insideReportedMachineBed() || !m_core->knownOffset){denied("Aktueller G54-/Positionsreadback fehlt.");return;}
        c.stage="complete";c.reason="Sessionbezogener gemessener Fahrraum und G54-Zuordnung bestätigt. Absaugung und Materialfreigabe separat erforderlich.";
    } else denied("Dieser Einrichtungsschritt ist in der aktuellen Phase nicht zulässig.");
}
void ControllerRuntimeWorker::setupMotion(const ControllerRuntimeRequest &request) {
    auto &c=m_commissioning;
    if(m_accessory.activeLease()||m_jobActive||m_focusActive||m_core->firing||m_core->paused||m_core->busy()||!m_core->statusFresh()||
       !m_core->setupReadback.valid || c.fingerprint!=m_core->setupReadback.fingerprint)return;
    if(request.kind==ControllerRuntimeRequestKind::SetupHome && c.stage=="homeReady") {
        m_core->commissionedHomingContract=true;
        c.stage="homing";c.deadline=setupNow()+90000;c.reason="Referenzfahrt läuft; tatsächlichen Homeort anschließend bestätigen.";
        m_core->home();m_core->commissionedHomingContract=false; // Exactly one intended Home.
        if(!m_core->homingPending)cancelCommissioning("Referenzfahrt wurde nicht angenommen.");
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
    if(m_setupUpdating||!m_commissioning.active())return;
    QScopedValueRollback<bool> guard(m_setupUpdating,true);auto &c=m_commissioning;
    if(c.connectionEpoch!=m_connectionEpoch || !m_core->connected() || m_core->awaitingReset || (m_core->handshaking && !m_core->setupReadback.pending)) {cancelCommissioning("Verbindung/Reset/Neustart hat die Einrichtung entwertet.");return;}
    if(c.stage!="complete" && c.stage!="readback" && m_core->knownOutput && m_core->reportedOutput!=0) {cancelCommissioning("Unerwartete gemeldete Leistung während laserfreier Einrichtung.");return;}
    if(c.stage=="readback") {
        if(setupNow()>c.deadline){cancelCommissioning("Controllerdaten nicht vollständig innerhalb der Frist.");return;}
        if(m_core->setupReadback.complete) {
            if(!m_core->setupReadback.valid){cancelCommissioning(m_core->setupReadback.error);return;}
            if(m_core->setupReadyForAttestation()) {
                if(!closePoint(m_core->offset,m_core->setupReadback.g54)){cancelCommissioning("G54-Readback und aktueller WCO widersprechen sich.");return;}
                c.fingerprint=m_core->setupReadback.fingerprint;c.stage="mechanics";c.reason="Controllerdaten frisch gelesen. Endschalter, Richtung, freie Mechanik und deaktivierte Laseremission prüfen.";}
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
        if(c.stage=="homing") {
            if(!closePoint(m_core->offset,m_core->setupReadback.g54)){cancelCommissioning("Unerwartete WCO-Änderung während Referenzfahrt.");return;}
            c.home=m_core->reportedMachinePosition;c.stage="geometry";c.reason="Referenz protokolliert. Tatsächlichen Homeort und Pull-off beobachten und bestätigen.";}
        else {
            if(!closePoint(m_core->reportedMachinePosition,c.target)){cancelCommissioning("Einrichtungsziel und gemeldeter physischer Punkt widersprechen sich.");return;}
            c.stage=c.stage=="probePending"?"probeConfirm":"cornerConfirm";
            c.reason="Controller meldet Ziel erreicht/Idle/S0. Tatsächliche Strecke, Richtung und freien Abstand bestätigen.";
        }
    } else if(c.homeObserved && (!m_core->positionReferenced || !m_core->commissionedModalContract))cancelCommissioning("Maschinenreferenz oder Modalvertrag verloren.");
}
