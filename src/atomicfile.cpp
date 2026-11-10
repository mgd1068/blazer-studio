#include "atomicfile.h"
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
namespace AtomicFile {
bool write(const QString &path, const QByteArray &bytes, QString &error,
           const QString &backupSuffix) {
    QLockFile lock(path + ".lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        error = "Datei wird bereits bearbeitet oder das Verzeichnis ist nicht beschreibbar.";
        return false;
    }
    if (QFileInfo::exists(path) && !backupSuffix.isEmpty()) {
        QFile source(path);
        QSaveFile backup(path + backupSuffix);
        if (!source.open(QIODevice::ReadOnly) || !backup.open(QIODevice::WriteOnly)) {
            error = "Sicherung konnte nicht angelegt werden.";
            return false;
        }
        while (!source.atEnd()) {
            auto block = source.read(65536);
            if (source.error() != QFile::NoError || backup.write(block) != block.size()) {
                error = "Sicherung konnte nicht vollständig geschrieben werden.";
                return false;
            }
        }
        if (!backup.commit()) {
            error = "Sicherung konnte nicht abgeschlossen werden.";
            return false;
        }
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        error = "Datei konnte nicht vollständig gespeichert werden. Der vorherige Stand bleibt "
                "erhalten.";
        return false;
    }
    return true;
}
} // namespace AtomicFile
