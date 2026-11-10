#include "imageprocessing.h"
#include <algorithm>
#include <cmath>
bool ImageProcessing::validate(const QSize &source, const QVariantMap &s, QString &error) {
    double brightness = s.value("brightness", 0).toDouble(),
           contrast = s.value("contrast", 1).toDouble(), gamma = s.value("gamma", 1).toDouble();
    QString mode = s.value("imageMode", "dither").toString();
    double threshold = s.value("threshold", 128).toDouble();
    double sharpness = s.value("sharpness", 0).toDouble();
    if (!std::isfinite(sharpness) || sharpness < 0 || sharpness > 3 || !source.isValid() ||
        !std::isfinite(brightness) || brightness < -1 || brightness > 1 ||
        !std::isfinite(contrast) || contrast < 0 || contrast > 3 || !std::isfinite(gamma) ||
        gamma < .1 || gamma > 5 || !std::isfinite(threshold) || threshold < 0 || threshold > 255 ||
        !QStringList{"dither", "atkinson", "threshold", "grayscale"}.contains(mode)) {
        error = "Ungültige Bildeinstellungen.";
        return false;
    }
    QRect crop(s.value("cropX", 0).toInt(), s.value("cropY", 0).toInt(),
               s.value("cropW", source.width()).toInt(), s.value("cropH", source.height()).toInt());
    if (crop.isEmpty() || !QRect(QPoint(), source).contains(crop)) {
        error = "Bildzuschnitt außerhalb des Originals.";
        return false;
    }
    return true;
}
QImage ImageProcessing::apply(const QImage &source, const QVariantMap &s, QString &error,
                              const Cancellation &cancel) {
    if (isCancelled(cancel)) {
        error = "Berechnung abgebrochen.";
        return {};
    }
    if (!validate(source.size(), s, error))
        return {};
    double brightness = s.value("brightness", 0).toDouble(),
           contrast = s.value("contrast", 1).toDouble(), gamma = s.value("gamma", 1).toDouble();
    QString mode = s.value("imageMode", "dither").toString();
    double threshold = s.value("threshold", 128).toDouble(),
           sharpness = s.value("sharpness", 0).toDouble();
    QRect crop(s.value("cropX", 0).toInt(), s.value("cropY", 0).toInt(),
               s.value("cropW", source.width()).toInt(), s.value("cropH", source.height()).toInt());
    auto image = source.copy(crop).convertToFormat(QImage::Format_ARGB32);
    auto original = image;
    const bool invert = s.value("invert", false).toBool(),
               ellipseMask = s.value("ellipseMask", false).toBool(),
               grayscale = mode == "grayscale", floyd = mode == "dither",
               atkinson = mode == "atkinson";
    int w = image.width(), h = image.height();
    QVector<double> errors[3] = {QVector<double>(w + 4), QVector<double>(w + 4),
                                 QVector<double>(w + 4)};
    for (int y = 0; y < h; ++y) {
        if (isCancelled(cancel)) {
            error = "Berechnung abgebrochen.";
            return {};
        }
        double *current = errors[y % 3].data(), *next = errors[(y + 1) % 3].data(),
               *afterNext = errors[(y + 2) % 3].data();
        for (int x = 0; x < w; ++x) {
            auto c = original.pixelColor(x, y);
            if (c.alpha() == 0)
                continue;
            double gray = (qGray(c.rgb()) * c.alpha() + 255. * (255 - c.alpha())) / 255.;
            if (sharpness > 0) {
                double sum = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        auto n = original.pixelColor(std::clamp(x + dx, 0, w - 1),
                                                     std::clamp(y + dy, 0, h - 1));
                        sum += (qGray(n.rgb()) * n.alpha() + 255. * (255 - n.alpha())) / 255.;
                    }
                gray = std::clamp(gray + sharpness * (gray - sum / 9), 0., 255.);
            }
            gray = std::clamp((gray / 255. - .5) * contrast + .5 + brightness, 0., 1.);
            gray = std::pow(gray, gamma);
            if (invert)
                gray = 1 - gray;
            if (ellipseMask && std::pow((x + .5 - w / 2.) / (w / 2.), 2) +
                                       std::pow((y + .5 - h / 2.) / (h / 2.), 2) >
                                   1) {
                image.setPixelColor(x, y, Qt::transparent);
                continue;
            }
            int value = qRound(gray * 255);
            if (!grayscale) {
                double adjusted = value + current[x + 2];
                int binary = adjusted < threshold ? 0 : 255;
                double e = adjusted - binary;
                value = binary;
                auto add = [&](int dx, int dy, double fraction) {
                    if (x + dx >= 0 && x + dx < w && y + dy < h) {
                        auto *destination = dy == 0 ? current : dy == 1 ? next : afterNext;
                        destination[x + dx + 2] += e * fraction;
                    }
                };
                if (floyd) {
                    add(1, 0, 7. / 16);
                    add(-1, 1, 3. / 16);
                    add(0, 1, 5. / 16);
                    add(1, 1, 1. / 16);
                } else if (atkinson) {
                    for (auto p : {QPoint(1, 0), QPoint(2, 0), QPoint(-1, 1), QPoint(0, 1),
                                   QPoint(1, 1), QPoint(0, 2)})
                        add(p.x(), p.y(), 1. / 8);
                }
            }
            image.setPixelColor(x, y, QColor(value, value, value));
        }
        std::fill(current, current + w + 4, 0.);
    }
    return image;
}
