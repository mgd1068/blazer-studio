#include "imageprocessing.h"
#include <algorithm>
#include <cmath>
QImage ImageProcessing::apply(const QImage &source, const QVariantMap &s, QString &error) {
    double brightness = s.value("brightness", 0).toDouble(),
           contrast = s.value("contrast", 1).toDouble(), gamma = s.value("gamma", 1).toDouble();
    QString mode = s.value("imageMode", "dither").toString();
    double threshold = s.value("threshold", 128).toDouble();
    double sharpness = s.value("sharpness", 0).toDouble();
    if (!std::isfinite(sharpness) || sharpness < 0 || sharpness > 3 || source.isNull() ||
        !std::isfinite(brightness) || brightness < -1 || brightness > 1 ||
        !std::isfinite(contrast) || contrast < 0 || contrast > 3 || !std::isfinite(gamma) ||
        gamma < .1 || gamma > 5 || !std::isfinite(threshold) || threshold < 0 || threshold > 255 ||
        !QStringList{"dither", "atkinson", "threshold", "grayscale"}.contains(mode)) {
        error = "Ungültige Bildeinstellungen.";
        return {};
    }
    QRect crop(s.value("cropX", 0).toInt(), s.value("cropY", 0).toInt(),
               s.value("cropW", source.width()).toInt(), s.value("cropH", source.height()).toInt());
    if (crop.isEmpty() || !source.rect().contains(crop)) {
        error = "Bildzuschnitt außerhalb des Originals.";
        return {};
    }
    auto image = source.copy(crop).convertToFormat(QImage::Format_ARGB32);
    auto original = image;
    int w = image.width(), h = image.height();
    QVector<double> errors((w + 4) * (h + 2));
    for (int y = 0; y < h; ++y)
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
            if (s.value("invert", false).toBool())
                gray = 1 - gray;
            if (s.value("ellipseMask", false).toBool() &&
                std::pow((x + .5 - w / 2.) / (w / 2.), 2) +
                        std::pow((y + .5 - h / 2.) / (h / 2.), 2) >
                    1) {
                image.setPixelColor(x, y, Qt::transparent);
                continue;
            }
            int value = qRound(gray * 255);
            if (mode != "grayscale") {
                double adjusted = value + errors[y * (w + 4) + x + 2];
                int binary = adjusted < threshold ? 0 : 255;
                double e = adjusted - binary;
                value = binary;
                auto add = [&](int dx, int dy, double fraction) {
                    if (x + dx >= 0 && x + dx < w && y + dy < h)
                        errors[(y + dy) * (w + 4) + x + dx + 2] += e * fraction;
                };
                if (mode == "dither") {
                    add(1, 0, 7. / 16);
                    add(-1, 1, 3. / 16);
                    add(0, 1, 5. / 16);
                    add(1, 1, 1. / 16);
                } else if (mode == "atkinson") {
                    for (auto p : {QPoint(1, 0), QPoint(2, 0), QPoint(-1, 1), QPoint(0, 1),
                                   QPoint(1, 1), QPoint(0, 2)})
                        add(p.x(), p.y(), 1. / 8);
                }
            }
            image.setPixelColor(x, y, QColor(value, value, value));
        }
    return image;
}
