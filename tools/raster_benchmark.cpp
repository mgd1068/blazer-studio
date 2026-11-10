#include "imageprocessing.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <sys/resource.h>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    bool ok = false;
    int n = app.arguments().value(1, "2048").toInt(&ok);
    if (!ok || n < 16 || n > 4096)
        return 2;
    QImage input(n, n, QImage::Format_ARGB32);
    for (int y = 0; y < n; ++y) {
        auto *row = reinterpret_cast<QRgb *>(input.scanLine(y));
        for (int x = 0; x < n; ++x) {
            int v = (x * 191 / (n - 1) + y * 63 / (n - 1) + ((x / 7 + y / 11) % 2) * 19) % 256;
            row[x] = qRgb(v, v, v);
        }
    }
    QString mode = app.arguments().value(2, "dither");
    QString error;
    QElapsedTimer clock;
    clock.start();
    auto result = ImageProcessing::apply(input, {{"imageMode", mode}}, error);
    double ms = clock.nsecsElapsed() / 1e6;
    if (result.isNull())
        return 3;
    auto hash =
        QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(result.constBits()),
                                                result.sizeInBytes()),
                                 QCryptographicHash::Sha256)
            .toHex();
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    // /proc resets the memory high-water mark at exec; ru_maxrss can retain
    // a parent's pre-exec high-water mark on Linux.
    qint64 peakRss = usage.ru_maxrss;
    QFile status("/proc/self/status");
    if (status.open(QIODevice::ReadOnly))
        for (const auto &line : status.readAll().split('\n'))
            if (line.startsWith("VmHWM:"))
                peakRss = line.simplified().split(' ').value(1).toLongLong();
    auto json = QJsonDocument(QJsonObject{{"size", n},
                                          {"mode", mode},
                                          {"milliseconds", ms},
                                          {"peakRssKiB", double(peakRss)},
                                          {"sha256", QString::fromLatin1(hash)},
                                          {"error", error}})
                    .toJson(QJsonDocument::Compact);
    printf("%s\n", json.constData());
    return 0;
}
