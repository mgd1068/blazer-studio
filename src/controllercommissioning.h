#pragma once
#include <QVariantMap>
#include <QRectF>
#include <QPointF>

// Session-only operator evidence. No deserializer and no persisted executable grant.
// Firmware observations are collected separately by GrblSession and bound to the
// current connection/configuration epoch before the worker accepts an attestation.
struct ControllerCommissioning {
    QString stage = "inactive", reason;
    QByteArray fingerprint;
    QRectF limits;
    QPointF home, target;
    int cornerIndex = 0, cornersConfirmed = 0, probesConfirmed = 0;
    bool xConfirmed = false, yConfirmed = false, homeObserved = false;
    bool active() const { return stage != "inactive" && stage != "fault"; }
    bool moving() const { return stage == "homing" || stage == "probePending" || stage == "cornerPending"; }
};
// Public GUI contract is exposed by Machine:
// commissioning: readonly map {stage,reason,settings,fingerprint,machineX/Y,
// workX/Y,wcoX/Y,minX,minY,maxX,maxY,cornerIndex,cornersConfirmed,
// probesConfirmed,homeObserved,active,ready}.
// beginCommissioning(), attestCommissioningMechanics(switches,direction,
// freeTravel,laserDisabled), commissioningHome(), confirmCommissioningHome(),
// commissioningProbe(dx,dy), confirmCommissioningProbe(),
// setCommissioningLimits(minX,minY,maxX,maxY), commissioningCorner(index),
// confirmCommissioningCorner(), confirmCommissioningCoordinates(), cancelCommissioning().
// Movement controls register native actions setupHome, setupProbe(dx/dy),
// setupCorner(index). Operator attestations are data, never public grant flags.
