#include "registration.h"
#include <QImageReader>
#include <QPolygonF>
#include <cmath>
bool Registration::load(const QUrl &file) {
    if (!file.isLocalFile())
        return false;
    QImageReader reader(file.toLocalFile());
    auto size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 16000000)
        return false;
    auto frame = reader.read();
    if (frame.isNull())
        return false;
    image = frame;
    url = file;
    valid = shown = false;
    emit changed();
    return true;
}
bool Registration::calibrate(const QVariantList &pixels, const QVariantList &world) {
    if (image.isNull() || pixels.size() != 8 || world.size() != 8)
        return false;
    QPolygonF source, target;
    for (int i = 0; i < 8; i += 2) {
        bool a, b, c, d;
        double x = pixels[i].toDouble(&a), y = pixels[i + 1].toDouble(&b),
               wx = world[i].toDouble(&c), wy = world[i + 1].toDouble(&d);
        if (!a || !b || !c || !d || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(wx) ||
            !std::isfinite(wy) || x < 0 || y < 0 || x > image.width() || y > image.height() ||
            wx < 0 || wy < 0 || wx > 200 || wy > 150)
            return false;
        source.append({x, y});
        target.append({wx, wy});
    }
    auto convex = [](const QPolygonF &p) {
        double sign = 0;
        for (int i = 0; i < 4; ++i) {
            auto a = p[(i + 1) % 4] - p[i], b = p[(i + 2) % 4] - p[(i + 1) % 4];
            double cross = a.x() * b.y() - a.y() * b.x();
            if (std::abs(cross) < 1e-6)
                return false;
            if (sign * cross < 0)
                return false;
            sign = cross;
        }
        return true;
    };
    QTransform t;
    if (!convex(source) || !convex(target) || !QTransform::quadToQuad(source, target, t) ||
        !t.isInvertible())
        return false;
    mapping = t;
    valid = shown = true;
    emit changed();
    return true;
}
