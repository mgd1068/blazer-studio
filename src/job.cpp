#include "job.h"
#include "document.h"
#include "geometry.h"
#include <QFont>
#include <QPaintDevice>
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPathStroker>
#include <QSvgRenderer>
#include <algorithm>
#include <cmath>
namespace {
constexpr int maxMoves = 500000;
class Capture : public QPaintEngine {
  public:
    Capture() : QPaintEngine(QPaintEngine::AllFeatures) {}
    QPainterPath path, area;
    QBrush brush;
    QPen pen;
    QTransform transform;
    double opacity = 1;
    int elements = 0;
    bool unsupported = false;
    bool begin(QPaintDevice *) override {
        setActive(true);
        return true;
    }
    bool end() override {
        setActive(false);
        return true;
    }
    Type type() const override { return User; }
    void updateState(const QPaintEngineState &s) override {
        if (s.state() & DirtyOpacity)
            opacity = s.opacity();
        if (s.state() & DirtyBrush)
            brush = s.brush();
        if (s.state() & DirtyPen)
            pen = s.pen();
        if (s.state() & DirtyTransform)
            transform = s.transform();
        if (s.state() & (DirtyClipPath | DirtyClipRegion))
            unsupported = true;
    }
    void drawPath(const QPainterPath &p) override {
        if (opacity <= 0)
            return;
        const bool filled = brush.style() != Qt::NoBrush && brush.color().alpha() > 0;
        const bool stroked =
            pen.style() != Qt::NoPen && pen.color().alpha() > 0 && pen.widthF() > 0;
        if (!filled && !stroked)
            return;
        elements += p.elementCount();
        if (elements > 1000000) {
            unsupported = true;
            return;
        }
        auto mapped = transform.map(p);
        path.addPath(mapped);
        auto apply = [&](const QPainterPath &ink, const QColor &color) {
            if (color.red() == 255 && color.green() == 255 && color.blue() == 255)
                area = area.subtracted(ink);
            else
                area = area.isEmpty() ? ink : area.united(ink);
        };
        if (filled)
            apply(mapped, brush.color());
        if (stroked) {
            QPainterPathStroker stroker(pen);
            apply(transform.map(stroker.createStroke(p)), pen.color());
        }
    }
    void drawPolygon(const QPointF *p, int n, PolygonDrawMode mode) override {
        if (n < 1)
            return;
        QPainterPath q;
        q.moveTo(p[0]);
        for (int i = 1; i < n; ++i)
            q.lineTo(p[i]);
        if (mode != PolylineMode)
            q.closeSubpath();
        drawPath(q);
    }
    void drawPixmap(const QRectF &, const QPixmap &, const QRectF &) override {
        unsupported = true;
    }
    void drawImage(const QRectF &, const QImage &, const QRectF &,
                   Qt::ImageConversionFlags) override {
        unsupported = true;
    }
    void drawTextItem(const QPointF &p, const QTextItem &text) override {
        QPainterPath q;
        q.addText(p, text.font(), text.text());
        auto previous = brush;
        brush = QBrush(pen.color());
        drawPath(q);
        brush = previous;
    }
};
class CaptureDevice : public QPaintDevice {
  public:
    mutable Capture engine;
    QPaintEngine *paintEngine() const override { return &engine; }
    int metric(PaintDeviceMetric m) const override {
        switch (m) {
        case PdmWidth:
        case PdmHeight:
            return 10000;
        case PdmWidthMM:
        case PdmHeightMM:
            return 2646;
        case PdmDpiX:
        case PdmDpiY:
        case PdmPhysicalDpiX:
        case PdmPhysicalDpiY:
            return 96;
        case PdmDepth:
            return 32;
        case PdmDevicePixelRatio:
            return 1;
        case PdmDevicePixelRatioScaled:
            return 65536;
        default:
            return 0;
        }
    }
};
QString num(double n) {
    if (std::abs(n) < .0005)
        n = 0;
    return QString::number(n, 'f', 3);
}
} // namespace
QPainterPath JobPlanner::geometry(const Artwork &a, QString &error, bool fill) {
    QPainterPath p;
    if (a.kind == "rect")
        p.addRect(a.bounds);
    else if (a.kind == "ellipse")
        p.addEllipse(a.bounds);
    else if (a.kind == "text") {
        QFont font("DejaVu Sans");
        font.setWeight(a.text == "MOIN" ? QFont::Black : QFont::DemiBold);
        font.setPixelSize(100);
        p.addText(0, 100, font, a.text);
        auto b = p.boundingRect();
        if (b.isEmpty()) {
            error = "Text besitzt keine Konturen.";
            return {};
        }
        QTransform t;
        t.translate(a.bounds.x(), a.bounds.y() + a.bounds.height());
        t.scale(a.bounds.width() / b.width(), -a.bounds.height() / b.height());
        t.translate(-b.x(), -b.y());
        p = t.map(p);
    } else if (a.kind == "svg" && a.asset) {
        CaptureDevice device;
        QPainter painter(&device);
        QSvgRenderer renderer(a.asset->bytes);
        renderer.setAnimationEnabled(false);
        renderer.render(&painter, QRectF(0, 0, a.bounds.width(), a.bounds.height()));
        painter.end();
        if (device.engine.unsupported) {
            error = "SVG enthält nicht unterstützte Raster- oder Clip-Inhalte.";
            return {};
        }
        QTransform t;
        t.translate(a.bounds.x(), a.bounds.y() + a.bounds.height());
        t.scale(1, -1);
        p = t.map(fill ? device.engine.area : device.engine.path);
    } else
        error = "Objektformat kann nicht als Laserpfad ausgegeben werden.";
    return p;
}
JobPlan JobPlanner::build(const Document &d) {
    JobPlan job;
    QPointF head(0, 0);
    auto add = [&](QPointF point, double feed, double power, int layer, bool rapid) {
        if (job.moves.size() >= maxMoves) {
            if (!job.errors.contains("Job überschreitet 500000 Segmente."))
                job.errors << "Job überschreitet 500000 Segmente.";
            return false;
        }
        if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || point.x() < -.00001 ||
            point.y() < -.00001 || point.x() > 200.00001 || point.y() > 150.00001) {
            job.errors << "Laserpfad außerhalb des Arbeitsraums.";
            return false;
        }
        const double length = QLineF(head, point).length();
        if (length < .000001)
            return true;
        job.moves.append({head, point, feed, power, layer, rapid});
        (rapid ? job.travelLength : job.workLength) += length;
        job.seconds += length / feed * 60;
        head = point;
        return true;
    };
    struct Shape {
        QPainterPath path;
        QVariantMap settings;
        int layer;
    };
    QList<Shape> shapes;
    for (const auto &a : d.items()) {
        auto settings = d.layers()[a.layer].toMap();
        for (auto it = a.settings.begin(); it != a.settings.end(); ++it)
            settings[it.key()] = it.value();
        if (!a.visible || !settings["enabled"].toBool() || settings["power"].toDouble() <= 0)
            continue;
        if (!Geometry::insideBed(a.bounds)) {
            job.errors << a.name + ": außerhalb des Arbeitsraums.";
            continue;
        }
        QString error;
        auto path = geometry(a, error, a.layer == 0);
        if (!error.isEmpty() || path.isEmpty()) {
            job.errors << a.name + ": " + (error.isEmpty() ? "keine Konturen" : error);
            continue;
        }
        bool finite = true;
        for (int i = 0; i < path.elementCount(); ++i) {
            auto point = path.elementAt(i);
            if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
                finite = false;
                break;
            }
        }
        const auto extent = path.boundingRect();
        if (!finite || !std::isfinite(extent.width()) || !std::isfinite(extent.height()) ||
            extent.left() < -.00001 || extent.top() < -.00001 || extent.right() > 200.00001 ||
            extent.bottom() > 150.00001) {
            job.errors << a.name + ": Kontur außerhalb des Arbeitsraums oder ungültig.";
            continue;
        }
        shapes.append({path, settings, a.layer});
    }
    std::stable_sort(shapes.begin(), shapes.end(), [](const Shape &a, const Shape &b) {
        if (a.layer != b.layer)
            return a.layer < b.layer;
        auto aa = a.path.boundingRect(), bb = b.path.boundingRect();
        return aa.width() * aa.height() < bb.width() * bb.height();
    });
    for (const auto &shape : shapes) {
        const auto &settings = shape.settings;
        double speed = settings["speed"].toDouble(), power = settings["power"].toDouble();
        int passes = settings["passes"].toInt();
        if (!std::isfinite(speed) || speed < 1 || speed > 100000 || !std::isfinite(power) ||
            power < 0 || power > 100 || passes < 1 || passes > 100) {
            job.errors << "Ungültige Bearbeitungsparameter.";
            continue;
        }
        auto polygons = shape.path.toSubpathPolygons(QTransform::fromScale(50, 50));
        for (auto &poly : polygons)
            for (auto &point : poly)
                point /= 50.;
        if (shape.layer == 1)
            std::stable_sort(polygons.begin(), polygons.end(), [](const auto &a, const auto &b) {
                auto aa = a.boundingRect(), bb = b.boundingRect();
                return aa.width() * aa.height() < bb.width() * bb.height();
            });
        for (int pass = 0; pass < passes && job.errors.isEmpty(); ++pass) {
            if (shape.layer == 1) {
                for (const auto &poly : polygons) {
                    if (poly.size() < 2)
                        continue;
                    if (!add(poly.first(), 3000, 0, 1, true))
                        break;
                    for (int i = 1; i < poly.size(); ++i)
                        if (!add(poly[i], speed, power, 1, false))
                            break;
                }
            } else {
                const double interval = settings.value("interval", .1).toDouble();
                if (!std::isfinite(interval) || interval < .001 || interval > 10) {
                    job.errors << "Ungültiges Gravurintervall.";
                    break;
                }
                const auto box = shape.path.boundingRect();
                const int rowCount = std::max(0, int(std::ceil(box.height() / interval - .5)));
                QVector<QList<QPair<double, int>>> rows(rowCount);
                int crossings = 0;
                for (const auto &poly : polygons)
                    for (int i = 0; i < poly.size() && job.errors.isEmpty(); ++i) {
                        auto a = poly[i], b = poly[(i + 1) % poly.size()];
                        if (a.y() == b.y())
                            continue;
                        int first = std::max(
                            0,
                            int(std::ceil((std::min(a.y(), b.y()) - box.top()) / interval - .5)));
                        int last = std::min(
                            rowCount,
                            int(std::ceil((std::max(a.y(), b.y()) - box.top()) / interval - .5)));
                        for (int row = first; row < last; ++row) {
                            double y = box.top() + (row + .5) * interval;
                            rows[row].append(
                                {a.x() + (y - a.y()) * (b.x() - a.x()) / (b.y() - a.y()),
                                 b.y() > a.y() ? 1 : -1});
                            if (++crossings > 2 * maxMoves) {
                                job.errors << "Gravurgeometrie zu komplex.";
                                break;
                            }
                        }
                    }
                bool reverse = false;
                for (int row = 0; row < rowCount && job.errors.isEmpty(); ++row) {
                    const double y = box.top() + (row + .5) * interval;
                    auto &hits = rows[row];
                    std::sort(hits.begin(), hits.end(),
                              [](auto a, auto b) { return a.first < b.first; });
                    QList<QPair<double, double>> spans;
                    int winding = 0;
                    double start = 0;
                    for (auto hit : hits) {
                        bool before = shape.path.fillRule() == Qt::OddEvenFill ? (winding % 2 != 0)
                                                                               : (winding != 0);
                        winding += shape.path.fillRule() == Qt::OddEvenFill ? 1 : hit.second;
                        bool after = shape.path.fillRule() == Qt::OddEvenFill ? (winding % 2 != 0)
                                                                              : (winding != 0);
                        if (!before && after)
                            start = hit.first;
                        else if (before && !after && hit.first - start > .00001)
                            spans.append({start, hit.first});
                    }
                    if (reverse)
                        std::reverse(spans.begin(), spans.end());
                    for (auto span : spans) {
                        QPointF a(reverse ? span.second : span.first, y),
                            b(reverse ? span.first : span.second, y);
                        if (!add(a, 3000, 0, 0, true) || !add(b, speed, power, 0, false))
                            break;
                    }
                    reverse = !reverse;
                }
            }
        }
    }
    if (job.moves.isEmpty() && job.errors.isEmpty())
        job.errors << "Keine aktiven Laserpfade vorhanden.";
    if (!job.errors.isEmpty()) {
        job.moves.clear();
        job.workLength = job.travelLength = job.seconds = 0;
    }
    return job;
}
QStringList JobPlan::gcode() const {
    if (!valid())
        return {};
    QStringList lines{"M5",  "(Blazer Studio: GRBL laser mode, $30=1000, $32=1, G54)",
                      "G21", "G90",
                      "G54", "G17",
                      "G94", "S0"};
    bool emitting = false;
    double lastPower = -1, lastFeed = -1;
    for (const auto &m : moves) {
        if (m.rapid) {
            if (emitting) {
                lines << "M5";
                emitting = false;
            }
            lines << "G0 X" + num(m.to.x()) + " Y" + num(m.to.y());
        } else {
            if (!emitting || m.power != lastPower) {
                lines << "M4 S" + QString::number(qRound(m.power * 10));
                emitting = true;
                lastPower = m.power;
            }
            auto line = "G1 X" + num(m.to.x()) + " Y" + num(m.to.y());
            if (m.speed != lastFeed) {
                line += " F" + num(m.speed);
                lastFeed = m.speed;
            }
            lines << line;
        }
    }
    lines << "M5 S0" << "G0";
    return lines;
}
QVariantMap JobPlan::summary() const {
    return {{"valid", valid()},
            {"errors", errors},
            {"segments", moves.size()},
            {"workLength", workLength},
            {"travelLength", travelLength},
            {"seconds", seconds}};
}

JobPlan JobPlanner::materialTest(double minSpeed, double maxSpeed, double minPower, double maxPower,
                                 int steps) {
    JobPlan job;
    if (!std::isfinite(minSpeed) || !std::isfinite(maxSpeed) || !std::isfinite(minPower) ||
        !std::isfinite(maxPower) || steps < 2 || steps > 10 || minSpeed < 1 || maxSpeed > 7000 ||
        minSpeed > maxSpeed || minPower < 0 || maxPower > 100 || minPower > maxPower) {
        job.errors << "Materialtest: 2–10 Stufen, 1–7000 mm/min und 0–100 % prüfen.";
        return job;
    }
    QPointF head(0, 0);
    auto add = [&](QPointF p, double feed, double power, bool rapid) {
        double distance = QLineF(head, p).length();
        job.moves.append({head, p, feed, power, 0, rapid});
        (rapid ? job.travelLength : job.workLength) += distance;
        job.seconds += distance / feed * 60;
        head = p;
    };
    for (int row = 0; row < steps; ++row)
        for (int column = 0; column < steps; ++column) {
            double speed = minSpeed + (maxSpeed - minSpeed) * row / (steps - 1),
                   power = minPower + (maxPower - minPower) * column / (steps - 1);
            for (int line = 0; line < 50; ++line) {
                double y = 10 + row * 8 + (line + .5) * .1;
                double left = 10 + column * 8, right = left + 5;
                add({line % 2 ? right : left, y}, 3000, 0, true);
                add({line % 2 ? left : right, y}, speed, power, false);
            }
        }
    return job;
}
