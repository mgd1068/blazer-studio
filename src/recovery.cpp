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
    decisionPending = available();
    timer.setSingleShot(true);
    timer.setInterval(1500);
    connect(d, &Document::changed, this, [this] {
        if (decisionPending || sessionClosed)
            return;
        if (document->dirty())
            timer.start();
        else if (written || restored)
            clear();
    });
    connect(&timer, &QTimer::timeout, this, &Recovery::save);
}
bool Recovery::available() const {
    return writable() && (QFile::exists(path) || QFile::exists(path + ".previous"));
}
bool Recovery::save() {
    if (!writable() || decisionPending || sessionClosed)
        return false;
    if (!document->dirty())
        return true;
    auto bytes = document->projectBytes();
    if (bytes.size() > ImageLimits::projectBytes)
        return false;
    QString error;
    bool ok =
        AtomicFile::write(path, bytes, error, preservePrevious ? QString() : QString(".previous"));
    if (ok) {
        written = true;
        preservePrevious = false;
    } else
        document->notify("Automatische Sicherung fehlgeschlagen: " + error);
    emit changed();
    return ok;
}
bool Recovery::restore() {
    if (!writable() || sessionClosed)
        return false;
    for (const QString &candidate : {path, path + ".previous"}) {
        QFile f(candidate);
        if (f.open(QIODevice::ReadOnly) &&
            document->restoreBytes(f.read(ImageLimits::projectBytes + 1))) {
            preservePrevious = candidate != path;
            restored = true;
            decisionPending = false;
            timer.start();
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
    if (!writable())
        return;
    timer.stop();
    for (const auto &candidate : {path, path + ".previous"}) {
        if (QFile::exists(candidate) && !QFile::remove(candidate)) {
            document->notify("Automatische Sicherung konnte nicht entfernt werden: " + candidate);
            emit changed();
            return;
        }
    }
    written = false;
    restored = false;
    preservePrevious = false;
    decisionPending = false;
    if (!sessionClosed && document->dirty())
        timer.start();
    emit changed();
}

void Recovery::closeSession() {
    sessionClosed = true;
    timer.stop();
    if (written || restored)
        clear();
}
