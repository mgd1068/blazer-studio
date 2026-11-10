#include "vectorutil.h"
#include <cmath>
QJsonArray VectorUtil::encode(const QPainterPath &path) {
    QJsonArray out;
    // setFillRule() may materialize a dummy MoveTo on an empty Qt path.
    // Keep empty geometry canonical across project save/open cycles.
    if (path.isEmpty())
        return out;
    for (int i = 0; i < path.elementCount(); ++i) {
        auto e = path.elementAt(i);
        out.append(QJsonArray{int(e.type), e.x, e.y});
    }
    return out;
}
bool VectorUtil::decode(const QJsonArray &data, QPainterPath &path) {
    if (data.size() > 100000)
        return false;
    QPainterPath p;
    for (int i = 0; i < data.size(); ++i) {
        auto a = data[i].toArray();
        if (a.size() != 3 || !a[0].isDouble() || !a[1].isDouble() || !a[2].isDouble() ||
            !std::isfinite(a[1].toDouble()) || !std::isfinite(a[2].toDouble()) ||
            std::abs(a[1].toDouble()) > 10000 || std::abs(a[2].toDouble()) > 10000)
            return false;
        double t = a[0].toDouble();
        if (t == 0)
            p.moveTo(a[1].toDouble(), a[2].toDouble());
        else if (t == 1 && i > 0)
            p.lineTo(a[1].toDouble(), a[2].toDouble());
        else if (t == 2 && i > 0 && i + 2 < data.size()) {
            auto b = data[++i].toArray(), c = data[++i].toArray();
            if (b.size() != 3 || c.size() != 3 || b[0] != 3 || c[0] != 3)
                return false;
            for (auto e : {b[1], b[2], c[1], c[2]})
                if (!e.isDouble() || !std::isfinite(e.toDouble()) || std::abs(e.toDouble()) > 10000)
                    return false;
            p.cubicTo(a[1].toDouble(), a[2].toDouble(), b[1].toDouble(), b[2].toDouble(),
                      c[1].toDouble(), c[2].toDouble());
        } else
            return false;
    }
    path = p;
    return true;
}
QString VectorUtil::svg(const QPainterPath &p) {
    QString out;
    auto n = [](double v) { return QString::number(v, 'g', 12); };
    for (int i = 0; i < p.elementCount(); ++i) {
        auto e = p.elementAt(i);
        if (e.isMoveTo())
            out += "M" + n(e.x) + " " + n(e.y);
        else if (e.isLineTo())
            out += "L" + n(e.x) + " " + n(e.y);
        else if (e.type == QPainterPath::CurveToElement) {
            auto b = p.elementAt(++i), c = p.elementAt(++i);
            out += "C" + n(e.x) + " " + n(e.y) + " " + n(b.x) + " " + n(b.y) + " " + n(c.x) + " " +
                   n(c.y);
        }
    }
    return out;
}
