#include "relief.h"
#include <QBuffer>
#include <QImageReader>
#include <algorithm>
#include <cmath>

QVariantMap ReliefSettings::toMap() const {
    return {{"passes", passes},
            {"interval", interval},
            {"gamma", gamma},
            {"inverted", inverted},
            {"depthPerPass", depthPerPass},
            {"crosshatch", crosshatch},
            {"cleanupEvery", cleanupEvery},
            {"cleanupPower", cleanupPower}};
}
bool ReliefSettings::fromMap(const QVariantMap &map, ReliefSettings &result) {
    ReliefSettings p;
    if ((map.size() != 5 && map.size() != 8) ||
        map.value("inverted").metaType().id() != QMetaType::Bool)
        return false;
    const auto bounded = [&](const QString &key, double lo, double hi, double &out) {
        auto v = map.value(key);
        if (v.metaType().id() != QMetaType::Double && v.metaType().id() != QMetaType::Int &&
            v.metaType().id() != QMetaType::LongLong)
            return false;
        out = v.toDouble();
        return std::isfinite(out) && out >= lo && out <= hi;
    };
    double passes = 0;
    if (!bounded("passes", 1, 256, passes) || std::floor(passes) != passes ||
        !bounded("interval", .02, 2, p.interval) || !bounded("gamma", .1, 5, p.gamma) ||
        !bounded("depthPerPass", 0, 1, p.depthPerPass))
        return false;
    if (map.size() == 8) {
        double every = 0;
        if (map.value("crosshatch").metaType().id() != QMetaType::Bool ||
            !bounded("cleanupEvery", 0, 256, every) || std::floor(every) != every ||
            !bounded("cleanupPower", 0, 100, p.cleanupPower))
            return false;
        p.crosshatch = map["crosshatch"].toBool();
        p.cleanupEvery = int(every);
    }
    p.passes = int(passes);
    p.inverted = map["inverted"].toBool();
    result = p;
    return true;
}
int ReliefSettings::count(QRgb pixel) const {
    return count(QRgba64::fromArgb32(pixel));
}
int ReliefSettings::count(QRgba64 pixel) const {
    if (pixel.alpha() == 0)
        return 0;
    double gray = (pixel.red() * 11. + pixel.green() * 16. + pixel.blue() * 5.) / (32. * 65535.);
    gray = gray * pixel.alpha() / 65535. + (1 - pixel.alpha() / 65535.);
    double darkness = inverted ? gray : 1 - gray;
    return std::clamp(int(std::ceil(std::pow(darkness, gamma) * passes - 1e-9)), 0, passes);
}
std::shared_ptr<const ReliefAsset> ReliefAsset::fromImage(const QImage &source) {
    if (source.isNull() || source.width() > 512 || source.height() > 512)
        return {};
    auto asset = std::make_shared<ReliefAsset>();
    asset->image =
        source.convertToFormat(source.depth() > 32 || source.format() == QImage::Format_Grayscale16
                                   ? QImage::Format_RGBA64
                                   : QImage::Format_ARGB32);
    for (int y = 0; y < asset->image.height(); ++y)
        for (int x = 0; x < asset->image.width(); ++x) {
            auto c = asset->image.pixelColor(x, y).rgba64();
            quint16 gray =
                (quint32(c.red()) * 11 + quint32(c.green()) * 16 + quint32(c.blue()) * 5) / 32;
            asset->image.setPixelColor(x, y, QColor::fromRgba64(gray, gray, gray, c.alpha()));
        }
    QBuffer buffer(&asset->png);
    buffer.open(QIODevice::WriteOnly);
    if (!asset->image.save(&buffer, "PNG"))
        return {};
    return asset;
}
std::shared_ptr<const ReliefAsset> ReliefAsset::parse(const QByteArray &png) {
    if (png.isEmpty() || png.size() > 2 * 1024 * 1024)
        return {};
    QBuffer buffer;
    buffer.setData(png);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "PNG");
    const auto size = reader.size();
    if (!size.isValid() || size.width() > 512 || size.height() > 512)
        return {};
    return fromImage(reader.read());
}

ReliefRaster ReliefRaster::build(const ReliefAsset &asset, const ReliefSettings &settings,
                                 const QSizeF &size) {
    ReliefRaster raster;
    ReliefSettings p;
    if (!ReliefSettings::fromMap(settings.toMap(), p) || asset.image.isNull() ||
        !std::isfinite(size.width()) || !std::isfinite(size.height()) || size.width() <= 0 ||
        size.height() <= 0 || size.width() > 2000 || size.height() > 2000) {
        raster.error = "Ungültige Reliefgeometrie.";
        return raster;
    }
    raster.columns = int(std::ceil(size.width() / p.interval));
    raster.rows = int(std::ceil(size.height() / p.interval));
    const qint64 cells = qint64(raster.columns) * raster.rows;
    if (cells > 1000000 || cells * p.passes > 20000000) {
        raster.error = "Relief zu aufwändig; Rasterabstand vergrößern oder Schichtzahl reduzieren.";
        return raster;
    }
    raster.counts.resize(int(cells));
    const auto &image = asset.image;
    for (int row = 0; row < raster.rows; ++row)
        for (int col = 0; col < raster.columns; ++col) {
            const double x = std::min((col + .5) * p.interval, size.width() - 1e-8);
            const double y = std::min((row + .5) * p.interval, size.height() - 1e-8);
            const int px = std::clamp(int(x / size.width() * image.width()), 0, image.width() - 1);
            const int py = std::clamp(image.height() - 1 - int(y / size.height() * image.height()),
                                      0, image.height() - 1);
            raster.counts[row * raster.columns + col] = p.count(image.pixelColor(px, py).rgba64());
        }
    return raster;
}
