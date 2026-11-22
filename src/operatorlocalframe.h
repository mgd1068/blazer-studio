#pragma once
#include "manualtravelcheck.h"
#include <QPointF>
#include <QVariantMap>

// Session-only worker state. Operator observations are explicitly not a sensor
// position or a global homing/reference grant. No JSON grant deserializer.
struct OperatorLocalFrameState {
    QString phase="inactive", reason;
    QByteArray frameId, approvalId, approvedArtifact;
    ManualTravelCheck::Distances available;
    QPointF confirmedHead, counterAtCapture, offsetAtCapture;
    quint64 connectionEpoch=0, frameEpoch=0, startupSequence=0, resultRevision=0;
    QByteArray settingsFingerprint;
    bool lastRequestAccepted=true;
    bool hasTravel=false, attested=false, captureReset=false, readingAfterReset=false;
    qint64 deadline=-1;
    static constexpr double safetyMarginMm=2.;
    bool engaged() const { return phase!="inactive"; }
};
// Machine GUI contract (readonly Q_PROPERTY QVariantMap operatorLocalFrame):
// phase inactive/reading/positioning/captureReady/resetting/capturing/preparing/
// travelReview/ready/running/paused/invalid; reason,frameId,approvalId,
// sourceFingerprint,coordinateEpoch,localPositionKnown/localX/localY,
// virtualHeadX/Y,marginMm(2),available/usableAfterMargin/requiredActualWire maps
// {negativeX,positiveX,negativeY,positiveY},fits,prepared,approved,startReady,
// motorIdleDelayMs,doorSubstate.
// Invokables:
// beginManualPlacement(); setManualTravel(negX,posX,negY,posY);
// attestManualPlacement(motorsFree,axesTravelMeasuredAndClear,headStationary,lidClosed);
// captureOperatorLocalFrame() [native localCapture]; prepareLocalJob();
// approveLocalTravel(QString approvalId); startLocalJob() [localStart];
// frameLocalJob() [localFrame]; resumeLocalJob() [localResume];
// cancelOperatorLocalFrame(). No method takes arbitrary GCode or grant flags.
