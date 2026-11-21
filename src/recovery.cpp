#include "recovery.h"
#include "atomicfile.h"
#include "imagelimits.h"
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
Recovery::Recovery(Document *d, const QString &directory, QObject *parent)
    : QObject(parent), document(d) {
    auto dir = directory.isEmpty()
                   ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recovery"
                   : directory;
    QDir().mkpath(dir);
    path = dir + "/autosave.blazer";
    lock = std::make_unique<QLockFile>(dir + "/session.lock");
    lock->setStaleLockTime(0);
    lock->tryLock(0);
    recoveryExists = writable() && (QFile::exists(path) || QFile::exists(path + ".previous"));
    decisionPending = available();
    timer.setSingleShot(true);
    timer.setInterval(1500);
    connect(d, &Document::changed, this, [this] {
        if (!document || decisionPending || sessionClosed)
            return;
        if (document->dirty()) {
            // Generic changed/status notifications must not postpone a pending
            // deferred retry forever, or replace an already queued idle retry.
            if (!workDeferred || !timer.isActive())
                timer.start(workDeferred ? 250 : 1500);
        } else if (written || restored)
            clear();
    });
    connect(&timer, &QTimer::timeout, this, &Recovery::retryDeferred);
    connect(d, &Document::synchronousWorkGuardChanged, this, [this] {
        // Queue work: never serialize inside a Machine state-change callback.
        if (workDeferred && !sessionClosed && document)
            timer.start(0);
    });
    connect(d, &QObject::destroyed, this, [this] {
        sessionClosed = true;
        timer.stop();
        document = nullptr;
        setDeferred(false);
    });
}
bool Recovery::available() const {
    return writable() && recoveryExists;
}
bool Recovery::save() {
    if (!document || !writable() || decisionPending || sessionClosed)
        return false;
    if (!document->dirty())
        return true;
    if (deferWhileBlocked())
        return false;
    clearPending = false;
    auto bytes = document->projectBytes();
    if (bytes.isEmpty() || bytes.size() > ImageLimits::projectBytes)
        return false;
    QString error;
    bool ok =
        AtomicFile::write(path, bytes, error, preservePrevious ? QString() : QString(".previous"));
    if (ok) {
        written = true;
        recoveryExists = true;
        preservePrevious = false;
    } else
        document->notify("Automatische Sicherung fehlgeschlagen: " + error);
    setDeferred(false);
    emit changed();
    return ok;
}
bool Recovery::restore() {
    if (!document || !writable() || sessionClosed || !document->permitSynchronousWork())
        return false;
    for (const QString &candidate : {path, path + ".previous"}) {
        QFile f(candidate);
        if (f.open(QIODevice::ReadOnly) &&
            document->restoreBytes(f.read(ImageLimits::projectBytes + 1))) {
            preservePrevious = candidate != path;
            restored = true;
            decisionPending = false;
            timer.start(1500);
            emit changed();
            document->notify(
                candidate == path
                    ? "Automatische Sicherung wiederhergestellt; bitte speichern."
                    : "Vorherige Sicherung wiederhergestellt; jüngste Sicherung war unlesbar.");
            return true;
        }
    }
    return false;
}
void Recovery::clear() {
    if (!document || !writable())
        return;
    if (deferWhileBlocked()) {
        clearPending = true;
        decisionPending = false; // User decision accepted; filesystem cleanup waits.
        emit changed();
        return;
    }
    clearPending = false;
    timer.stop();
    for (const auto &candidate : {path, path + ".previous"}) {
        if (QFile::exists(candidate) && !QFile::remove(candidate)) {
            document->notify("Automatische Sicherung konnte nicht entfernt werden: " + candidate);
            emit changed();
            return;
        }
    }
    recoveryExists = false;
    written = false;
    restored = false;
    preservePrevious = false;
    decisionPending = false;
    if (!sessionClosed && document && document->dirty())
        timer.start(1500);
    setDeferred(false);
    emit changed();
}

void Recovery::closeSession() {
    sessionClosed = true;
    timer.stop();
    if (written || restored)
        clear();
}

void Recovery::setDeferred(bool value) {
    if (workDeferred == value)
        return;
    workDeferred = value;
    emit changed();
}
bool Recovery::deferWhileBlocked() {
    if (!document || document->synchronousWorkBlockReason().isEmpty())
        return false;
    setDeferred(true);
    // Poll as a fallback if the owner did not emit its state notifier. No file IO.
    if (!sessionClosed)
        timer.start(250);
    return true;
}
void Recovery::retryDeferred() {
    if (!document || sessionClosed || decisionPending)
        return;
    if (document->dirty())
        save(); // Always the latest live document, never a stale deferred snapshot.
    else if (clearPending || written || restored)
        clear();
    else
        setDeferred(false);
}
