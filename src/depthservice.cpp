#include "depthservice.h"
#include <QDir>
#include <QFile>
#include <QStandardPaths>
DepthService::DepthService(Document *d, const QString &s, QObject *parent)
    : QObject(parent), document(d), script(s) {
    deadline.setSingleShot(true);
    deadline.setInterval(180000);
    connect(&deadline, &QTimer::timeout, this, [this] {
        status = "Zeitlimit erreicht.";
        process.kill();
    });
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        status = "Tiefenprozess fehlgeschlagen: " + process.errorString();
        emit changed();
    });
    connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus exit) {
                deadline.stop();
                if (code == 0 && exit == QProcess::NormalExit) {
                    bool imported =
                        document->importRelief(QUrl::fromLocalFile(temp.filePath("depth.png")));
                    status =
                        imported
                            ? "Relative KI-Tiefenkarte importiert; Geometrie prüfen. Keine Messung."
                            : "Ergebnis konnte nicht importiert werden.";
                } else
                    status = "Tiefenberechnung abgebrochen: " +
                             QString::fromUtf8(process.readAllStandardError()).right(1000);
                emit changed();
            });
}
bool DepthService::estimate(const QUrl &image) {
    if (busy() || !image.isLocalFile() || !temp.isValid())
        return false;
    auto runtime = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
                   "/blazer-studio/depth-runtime";
    auto python = runtime + "/bin/python";
    auto model = runtime + "/model_quantized.onnx";
    if (!QFile::exists(python) || !QFile::exists(model) || !QFile::exists(script)) {
        status = "Lokales Tiefenmodell fehlt. tools/setup_depth.py ausführen; danach funktioniert "
                 "die Berechnung offline.";
        emit changed();
        return false;
    }
    status = "Relative Tiefe wird lokal berechnet …";
    process.start(python,
                  {script, image.toLocalFile(), temp.filePath("depth.png"), "--model", model});
    deadline.start();
    emit changed();
    return true;
}
