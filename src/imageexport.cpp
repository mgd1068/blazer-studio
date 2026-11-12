#include "imageexport.h"
#include "imagelimits.h"
#include "imageprocessing.h"
#include <QBuffer>
#include <QFileInfo>
#include <QImageWriter>
#include <QLockFile>
#include <QPainter>
#include <QSaveFile>
#include <QtConcurrentRun>
#include <cmath>

namespace {
QByteArray format(const QUrl &target) {
    if (!target.isLocalFile())
        return {};
    auto suffix = QFileInfo(target.toLocalFile()).suffix().toLower().toLatin1();
    if (suffix == "jpeg")
        suffix = "jpg";
    return (suffix == "png" || suffix == "jpg" || suffix == "bmp") ? suffix : QByteArray();
}
} // namespace
ImageExport::ImageExport(QObject *parent) : QObject(parent) {
    m_pool.setMaxThreadCount(1);
}
ImageExport::~ImageExport() {
    cancel();
    disconnect(&m_watcher, nullptr, this, nullptr);
    m_pool.waitForDone();
}
void ImageExport::cancel() {
    if (m_cancel)
        m_cancel->store(true, std::memory_order_relaxed);
}
bool ImageExport::start(ImageExportInput input, const QUrl &target, QString &error) {
    if (m_active) {
        error = "Ein Bildexport läuft bereits.";
        return false;
    }
    if (format(target).isEmpty()) {
        error = "Bitte eine lokale PNG-, JPEG- oder BMP-Datei wählen.";
        return false;
    }
    m_active = true;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto token = m_cancel;
    disconnect(&m_watcher, nullptr, this, nullptr);
    connect(&m_watcher, &QFutureWatcher<ImageExportResult>::finished, this,
            [this, processed = input.processed, type = format(target)] {
                const auto result = m_watcher.result();
                m_active = false;
                emit changed();
                emit finished(result.cancelled ? "Bildexport abgebrochen; Zieldatei unverändert."
                              : !result.error.isEmpty()
                                  ? result.error
                                  : QString(processed ? "Bearbeitetes Bild"
                                                      : "Bild ohne aktuelle Korrekturen") +
                                        " als " + QString::fromLatin1(type.toUpper()) +
                                        " exportiert (" + QString::number(result.pixels.width()) +
                                        " × " + QString::number(result.pixels.height()) + " px).");
            });
    m_watcher.setFuture(QtConcurrent::run(&m_pool, [input = std::move(input), target, token] {
        try {
            return run(input, target, token);
        } catch (...) {
            return ImageExportResult{"Bildexport fehlgeschlagen (Ressourcen- oder Datenfehler)."};
        }
    }));
    emit changed();
    return true;
}
ImageExportResult ImageExport::run(const ImageExportInput &input, const QUrl &target,
                                   const Cancellation &cancel) {
    if (isCancelled(cancel))
        return {{}, {}, true};
    const auto type = format(target);
    if (type.isEmpty())
        return {"Bitte eine lokale PNG-, JPEG- oder BMP-Datei wählen."};
    const QFileInfo info(target.toLocalFile());
    if (info.isSymLink())
        return {"Bildexport benötigt einen eigenen Dateipfad statt eines Dateilinks."};
    if (!input.asset || input.asset->image.isNull() ||
        input.asset->image.width() > ImageLimits::side ||
        input.asset->image.height() > ImageLimits::side ||
        qint64(input.asset->image.width()) * input.asset->image.height() > ImageLimits::pixels ||
        !std::isfinite(input.sizeMm.width()) || !std::isfinite(input.sizeMm.height()) ||
        input.sizeMm.width() < .1 || input.sizeMm.height() < .1 || input.sizeMm.width() > 2000 ||
        input.sizeMm.height() > 2000)
        return {"Bilddaten oder physische Bildgröße ungültig."};
    QString error;
    auto image = input.processed
                     ? ImageProcessing::apply(input.asset->image, input.style, error, cancel)
                     : input.asset->image;
    if (isCancelled(cancel))
        return {{}, {}, true};
    if (!error.isEmpty() || image.isNull())
        return {error.isEmpty() ? "Bildverarbeitung fehlgeschlagen." : error};
    image.setDevicePixelRatio(1);
    // Start fresh to exclude source text/EXIF metadata. PNG preserves alpha and
    // raw 16-bit samples; JPEG/BMP explicitly flatten transparency onto white.
    QImage output(image.size(), type == "png" ? (image.depth() > 32 ? QImage::Format_RGBA64
                                                                    : QImage::Format_ARGB32)
                                              : QImage::Format_RGB32);
    if (output.isNull())
        return {"Bildspeicher nicht verfügbar."};
    output.fill(type == "png" ? Qt::transparent : Qt::white);
    QPainter painter(&output);
    if (type == "png")
        painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(QPoint(), image);
    painter.end();
    if (type == "png") {
        output.setDotsPerMeterX(qRound(output.width() * 1000. / input.sizeMm.width()));
        output.setDotsPerMeterY(qRound(output.height() * 1000. / input.sizeMm.height()));
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, type);
    if (type == "jpg")
        writer.setQuality(95);
    if (!writer.write(output))
        return {"Bildformat konnte nicht geschrieben werden: " + writer.errorString()};
    if (isCancelled(cancel))
        return {{}, {}, true};
    QLockFile lock(target.toLocalFile() + ".lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return {"Zieldatei wird bereits bearbeitet oder ist nicht beschreibbar."};
    QSaveFile file(target.toLocalFile());
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        return {
            "Bild konnte nicht vollständig gespeichert werden; vorheriger Stand bleibt erhalten."};
    if (isCancelled(cancel)) {
        file.cancelWriting();
        return {{}, {}, true};
    }
    if (!file.commit())
        return {"Bild konnte nicht veröffentlicht werden; vorheriger Stand bleibt erhalten."};
    return {{}, output.size(), false};
}
