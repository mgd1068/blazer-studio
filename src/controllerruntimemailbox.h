#pragma once
// Internal transport/test seam, not exposed to QML.
#include "controllerruntime.h"
#include <QMetaObject>
#include <QMutexLocker>
#include <deque>
enum class ControllerRuntimeRequestKind {
    Open,
    Jog,
    Home,
    Zero,
    Frame,
    Hold,
    Pause = Hold, // Source compatibility only: this request can NEVER resume.
    Resume,
    Override,
    Focus,
    FocusOff,
    Start,
    SetupData,
    SetupHome,
    SetupProbe,
    SetupCorner,
    SetupZero
};
struct ControllerRuntimeRequest {
    explicit ControllerRuntimeRequest(ControllerRuntimeRequestKind value) : kind(value) {}
    ControllerRuntimeRequestKind kind;
    quint64 id = 0, generation = 0;
    quint64 connectionEpoch = 0;
    quint64 focusGeneration = 0;
    qint64 issuedAt = 0, expiresAt = 0;
    QString endpoint;
    QByteArray binding;
    double x = 0, y = 0;
    int feed = 100, power = 100;
    QVariantMap bounds;
    ControllerRuntimeInputStamp input;
    PreparedControllerJob::Ptr prepared;
};
void retireControllerRequests(std::deque<ControllerRuntimeRequest> requests);
void retireControllerJob(PreparedControllerJob::Ptr job);
struct ControllerRuntimeMailbox {
    QMutex mutex;
    QObject *context = nullptr;
    std::deque<ControllerRuntimeRequest> normal;
    std::atomic<quint64> generation{1};
    std::atomic_bool stop{false}, close{false}, shutdown{false};
    std::atomic<quint64> focusGeneration{1}, focusWindowEpoch{1};
    std::atomic_bool focusOff{false};
    quint64 inputIssuer = 0;
    // Reservation spans pop -> core dispatch -> final snapshot publication. Nested
    // Core.changed publications cannot make a dequeued request appear idle.
    bool dispatchInFlight = false;
    bool continuityFence = false;
    bool wakePending = false, accepting = true;
    bool notificationPending = false;
    bool takeControl(std::atomic_bool &latch) {
        // status() observes the latch and reservation under this same mutex.
        // Transfer ownership before consuming the only public pending evidence.
        QMutexLocker lock(&mutex);
        if(!latch.exchange(false))return false;
        dispatchInFlight=true;
        return true;
    }
    ControllerStatus latest;
    QStringList log;
    QVariantMap diagnostic;
    qint64 deviceCapture = -1, heartbeatCapture = -1;
    QObject *guiContext = nullptr;
    void wakeLocked() {
        if (context && !wakePending) {
            wakePending = true;
            QMetaObject::invokeMethod(context, "drain", Qt::QueuedConnection);
        }
    }
    bool enqueue(ControllerRuntimeRequest request) {
        bool accepted = false;
        {
            QMutexLocker lock(&mutex);
            if (accepting && !continuityFence && normal.size() < 16 && context) {
                request.generation = generation.load();
                request.connectionEpoch = latest.connectionEpoch;
                request.focusGeneration = focusGeneration.load();
                normal.push_back(std::move(request));
                wakeLocked();
                accepted = true;
            }
        }
        if (!accepted)
            retireControllerJob(std::move(request.prepared));
        return accepted;
    }
    void revoke(bool closing, bool final = false) {
        // Revocation never grants authority and never waits for a worker slot.
        generation.fetch_add(1);
        stop.store(true);
        if (closing)
            close.store(true);
        if (final)
            shutdown.store(true);
        std::deque<ControllerRuntimeRequest> retired;
        {
            QMutexLocker lock(&mutex);
            if (final)
                accepting = false;
            normal.swap(retired);
            wakeLocked();
        }
        retireControllerRequests(std::move(retired));
    }
};
