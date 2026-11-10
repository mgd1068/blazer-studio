#include "recovery.h"
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
    timer.setSingleShot(true);
    timer.setInterval(1500);
    connect(d, &Document::changed, this, [this] {
        if (document->dirty())
            timer.start();
        else if (written)
            clear();
    });
    connect(&timer, &QTimer::timeout, this, &Recovery::save);
}
bool Recovery::available() const {
    return QFile::exists(path);
}
bool Recovery::save() {
    if (!document->dirty())
        return true;
    auto bytes = document->projectBytes();
    if (bytes.size() > 16 * 1024 * 1024)
        return false;
    if (QFile::exists(path)) {
        QFile::remove(path + ".previous");
        QFile::copy(path, path + ".previous");
    }
    QSaveFile f(path);
    bool ok = f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
    if (ok)
        written = true;
    else
        document->notify("Automatische Sicherung fehlgeschlagen: " + path);
    emit changed();
    return ok;
}
bool Recovery::restore() {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) && document->restoreBytes(f.read(16 * 1024 * 1024 + 1));
}
void Recovery::clear() {
    timer.stop();
    written = false;
    QFile::remove(path);
    QFile::remove(path + ".previous");
    emit changed();
}
