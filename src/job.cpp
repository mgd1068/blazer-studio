#include "job.h"
#include "document.h"
#include "geometry.h"
#include "imageprocessing.h"
#include <QDomDocument>
#include <QFont>
#include <QPaintDevice>
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPathStroker>
#include <QRegularExpression>
#include <QSet>
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
    bool unsupported = false, clipped = false;
    QPainterPath clip;
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
        if (s.state() & DirtyClipEnabled)
            clipped = s.isClipEnabled();
        if (s.state() & (DirtyClipPath | DirtyClipRegion)) {
            QPainterPath next;
            if (s.state() & DirtyClipPath)
                next = transform.map(s.clipPath());
            else
                for (const auto &rect : s.clipRegion())
                    next.addRect(transform.mapRect(QRectF(rect)));
            if (s.clipOperation() == Qt::IntersectClip && !clip.isEmpty())
                clip = clip.intersected(next);
            else
                clip = next;
            clipped = s.clipOperation() != Qt::NoClip;
        }
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
        path.addPath(clipped ? mapped.intersected(clip) : mapped);
        auto apply = [&](QPainterPath ink, const QColor &color) {
            if (clipped)
                ink = ink.intersected(clip);
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
QList<QDomElement> svgElements(QDomElement root) {
    QList<QDomElement> all{root};
    for (int i = 0; i < all.size(); ++i)
        for (auto child = all[i].firstChildElement(); !child.isNull();
             child = child.nextSiblingElement())
            all.append(child);
    return all;
}
QPainterPath svgGlobalClip(const QByteArray &bytes, const QRectF &target, bool &found,
                           QString &error) {
    QDomDocument doc;
    if (!doc.setContent(bytes)) {
        error = "SVG-XML ungültig.";
        return {};
    }
    auto all = svgElements(doc.documentElement());
    QList<QDomElement> owners;
    QDomElement definition;
    for (int i = 0; i < all.size(); ++i) {
        auto e = all.at(i).toElement();
        if (e.attribute("style").contains("clip-path")) {
            error = "CSS-Clips müssen vor dem Import in Pfade umgewandelt werden.";
            return {};
        }
        if (e.hasAttribute("clip-path"))
            owners.append(e);
    }
    if (owners.isEmpty())
        return {};
    found = true;
    if (owners.size() != 1) {
        error = "Mehrere SVG-Clips sind noch nicht unterstützt; vor dem Import vereinigen.";
        return {};
    }
    auto owner = owners.first();
    auto ref = owner.attribute("clip-path");
    QRegularExpression pattern("^url\\(\\s*#([^ )]+)\\s*\\)$");
    auto match = pattern.match(ref);
    if (!match.hasMatch()) {
        error = "SVG-Clipreferenz nicht unterstützt.";
        return {};
    }
    for (int i = 0; i < all.size(); ++i) {
        auto e = all.at(i).toElement();
        if (e.tagName().section(':', -1) == "clipPath" && e.attribute("id") == match.captured(1))
            definition = e;
    }
    if (definition.isNull() || definition.attribute("clipPathUnits") == "objectBoundingBox") {
        error = "SVG-Clip fehlt oder verwendet nicht unterstützte Bounding-Box-Koordinaten.";
        return {};
    }
    const QSet<QString> painted{"path",     "rect",    "circle", "ellipse", "line",
                                "polyline", "polygon", "text",   "image",   "use"};
    for (int i = 0; i < all.size(); ++i) {
        auto e = all.at(i).toElement();
        if (!painted.contains(e.tagName().section(':', -1)))
            continue;
        bool inside = false, inDefs = false;
        for (auto n = QDomNode(e); !n.isNull(); n = n.parentNode()) {
            auto tag = n.toElement().tagName().section(':', -1);
            inside |= n == owner;
            inDefs |= tag == "defs" || tag == "clipPath";
        }
        if (!inside && !inDefs) {
            error = "Gemischte SVG-Clipbereiche sind noch nicht unterstützt; vor dem Import "
                    "vereinigen.";
            return {};
        }
    }
    QDomDocument clipped;
    auto root = clipped.importNode(doc.documentElement(), false).toElement();
    clipped.appendChild(root);
    root.removeAttribute("clip-path");
    for (auto child = doc.documentElement().firstChildElement("defs"); !child.isNull();
         child = child.nextSiblingElement("defs"))
        root.appendChild(clipped.importNode(child, true));
    QList<QDomElement> ancestors;
    for (auto n = owner; !n.isNull() && n != doc.documentElement(); n = n.parentNode().toElement())
        ancestors.prepend(n);
    auto parent = root;
    for (auto ancestor : ancestors) {
        auto group = clipped.importNode(ancestor, false).toElement();
        group.setTagName("g");
        group.removeAttribute("clip-path");
        group.setAttribute("opacity", "1");
        parent.appendChild(group);
        parent = group;
    }
    auto mask = clipped.importNode(definition, true).toElement();
    mask.setTagName("g");
    parent.appendChild(mask);
    auto descendants = svgElements(mask);
    mask.setAttribute("fill", "black");
    mask.setAttribute("stroke", "none");
    for (int i = 0; i < descendants.size(); ++i) {
        auto e = descendants.at(i).toElement();
        e.setAttribute("fill", "black");
        e.setAttribute("stroke", "none");
        e.setAttribute("opacity", "1");
    }
    CaptureDevice device;
    QPainter painter(&device);
    QSvgRenderer renderer(clipped.toByteArray());
    renderer.setAnimationEnabled(false);
    renderer.render(&painter, target);
    painter.end();
    return device.engine.area;
}
QString num(double n) {
    if (std::abs(n) < .0005)
        n = 0;
    return QString::number(n, 'f', 3);
}
} // namespace
QPainterPath JobPlanner::geometry(const Artwork &a, QString &error, bool fill) {
    QPainterPath p;
    if (a.kind == "path") {
        QTransform t;
        t.translate(a.bounds.x(), a.bounds.y());
        t.scale(a.bounds.width(), a.bounds.height());
        p = t.map(a.path);
    } else if (a.kind == "rect")
        p.addRect(a.bounds);
    else if (a.kind == "ellipse")
        p.addEllipse(a.bounds);
    else if (a.kind == "text") {
        QFont font(a.style.value("font", "DejaVu Sans").toString());
        font.setWeight(a.text == "MOIN" ? QFont::Black : QFont::DemiBold);
        font.setPixelSize(100);
        font.setLetterSpacing(QFont::AbsoluteSpacing, a.style.value("spacing", 0).toDouble());
        if (a.style.contains("bold"))
            font.setBold(a.style["bold"].toBool());
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
        bool clipped = false;
        auto clip = svgGlobalClip(a.asset->bytes, QRectF(0, 0, a.bounds.width(), a.bounds.height()),
                                  clipped, error);
        if (!error.isEmpty())
            return {};
        auto converted = fill ? device.engine.area : device.engine.path;
        if (clipped)
            converted = converted.intersected(clip);
        p = t.map(converted);
    } else
        error = "Objektformat kann nicht als Laserpfad ausgegeben werden.";
    return p;
}
JobPlan JobPlanner::build(const Document &d) {
    return build(d.jobInput());
}
JobPlan JobPlanner::build(const JobInput &d, const Cancellation &cancel) {
    JobPlan job;
    auto aborted = [&]() {
        JobPlan result;
        result.cancelled = true;
        result.errors = {"Berechnung abgebrochen."};
        return result;
    };
    if (isCancelled(cancel))
        return aborted();
    if (d.layers.size() < 2 || d.layers.size() > 64 || d.activePage < 0 || d.activePage >= 100) {
        job.errors = {"Ungültiger Projektstand für die Berechnung."};
        return job;
    }
    for (const auto &art : d.items) {
        auto operations = art.extraOperations;
        operations.prepend(art.layer);
        for (int operation : operations)
            if (operation < 0 || operation >= d.layers.size()) {
                job.errors = {"Ungültige Operationszuordnung."};
                return job;
            }
    }
    QPointF head(0, 0);
    auto add = [&](QPointF point, double feed, double power, int layer, bool rapid, int slice = 0) {
        if (isCancelled(cancel))
            return false;
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
        job.moves.append({head, point, feed, power, layer, rapid, slice});
        ((rapid || power <= 0) ? job.travelLength : job.workLength) += length;
        job.minimumSeconds += length / feed * 60;
        double v = feed / 60., acceleration = 240.;
        job.seconds += length >= v * v / acceleration ? length / v + v / acceleration
                                                      : 2 * std::sqrt(length / acceleration);
        head = point;
        return true;
    };
    struct Shape {
        QPainterPath path;
        QVariantMap settings;
        int layer;
    };
    QList<Shape> shapes;
    QList<QPair<const Artwork *, int>> reliefs, images;
    for (const auto &original : d.items) {
        if (isCancelled(cancel))
            return aborted();
        if (original.page != d.activePage)
            continue;
        QList<int> operations{original.layer};
        operations.append(original.extraOperations);
        for (int op : operations) {
            if (isCancelled(cancel))
                return aborted();
            Artwork a = original;
            a.layer = op;
            auto settings = d.layers[a.layer].toMap();
            if (op == original.layer)
                for (auto it = a.settings.begin(); it != a.settings.end(); ++it)
                    settings[it.key()] = it.value();
            if (!a.visible || !settings["enabled"].toBool() || settings["power"].toDouble() <= 0)
                continue;
            if (!Geometry::insideBed(a.bounds)) {
                job.errors << a.name + ": außerhalb des Arbeitsraums.";
                continue;
            }
            if (a.kind == "image") {
                images.append({&original, op});
                continue;
            }
            if (a.kind == "relief") {
                reliefs.append({&original, op});
                continue;
            }
            QString error;
            auto path = geometry(a, error, settings["mode"] != "Schnitt");
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
    }
    std::stable_sort(shapes.begin(), shapes.end(), [](const Shape &a, const Shape &b) {
        if (a.settings["mode"] != b.settings["mode"])
            return a.settings["mode"] != "Schnitt";
        if (a.layer != b.layer)
            return a.layer < b.layer;
        auto aa = a.path.boundingRect(), bb = b.path.boundingRect();
        return aa.width() * aa.height() < bb.width() * bb.height();
    });
    // Layer-by-layer threshold slicing. All points of slice n finish before n+1.
    // A constant power/feed per slice makes the number of visits represent depth.
    qint64 reliefChecks = 0;
    for (const auto &item : reliefs) {
        if (isCancelled(cancel))
            return aborted();
        auto a = *item.first;
        a.layer = item.second;
        ReliefSettings p;
        auto settings = d.layers[a.layer].toMap();
        for (auto it = a.settings.begin(); it != a.settings.end(); ++it)
            settings[it.key()] = it.value();
        const double speed = settings["speed"].toDouble(), power = settings["power"].toDouble();
        if (d.layers[a.layer].toMap()["mode"] == "Schnitt" || !a.reliefAsset ||
            !ReliefSettings::fromMap(a.relief.toMap(), p) || !std::isfinite(speed) || speed < 1 ||
            speed > 7000 || !std::isfinite(power) || power <= 0 || power > 100) {
            job.errors << a.name + ": ungültige Reliefparameter (v: 1–7000 mm/min).";
            continue;
        }
        const auto raster = ReliefRaster::build(*a.reliefAsset, p, a.bounds.size(), cancel);
        if (!raster.error.isEmpty()) {
            job.errors << a.name + ": " + raster.error;
            continue;
        }
        reliefChecks += qint64(raster.counts.size()) * p.passes;
        if (reliefChecks > 20000000) {
            job.errors
                << "Relief-Gesamtjob zu aufwändig: maximal 20 Millionen Raster-/Schichtprüfungen.";
            break;
        }
        const int columns = raster.columns, rows = raster.rows;
        const auto &counts = raster.counts;
        for (int slice = 1; slice <= p.passes && job.errors.isEmpty(); ++slice) {
            if (isCancelled(cancel))
                return aborted();
            bool vertical = p.crosshatch && slice % 2 == 0;
            int scanRows = vertical ? columns : rows, scanColumns = vertical ? rows : columns;
            bool cleanup = p.cleanupEvery > 0 && slice % p.cleanupEvery == 0 && p.cleanupPower > 0;
            for (int phase = 0; phase < (cleanup ? 2 : 1) && job.errors.isEmpty(); ++phase) {
                double requestedPower = phase ? p.cleanupPower : power;
                for (int row = 0; row < scanRows && job.errors.isEmpty(); ++row) {
                    if (isCancelled(cancel))
                        return aborted();
                    bool reverse = (row + slice) % 2;
                    int start = -1;
                    auto point = [&](double edge) {
                        if (vertical)
                            return QPointF(
                                a.bounds.x() +
                                    std::min((row + .5) * p.interval, a.bounds.width() - 1e-8),
                                a.bounds.y() + std::min(edge * p.interval, a.bounds.height()));
                        return QPointF(a.bounds.x() + std::min(edge * p.interval, a.bounds.width()),
                                       a.bounds.y() + std::min((row + .5) * p.interval,
                                                               a.bounds.height() - 1e-8));
                    };
                    for (int step = 0; step <= scanColumns; ++step) {
                        int col = reverse ? scanColumns - 1 - step : step;
                        bool active =
                            step < scanColumns &&
                            counts[vertical ? col * columns + row : row * columns + col] >= slice;
                        if (active && start < 0)
                            start = step;
                        if (!active && start >= 0) {
                            double begin = reverse ? scanColumns - start : start,
                                   finish = reverse ? scanColumns - step : step;
                            int tag = phase ? -slice : slice;
                            if (!add(point(begin), 3000, 0, a.layer, true, tag) ||
                                !add(point(finish), speed, requestedPower, a.layer, false, tag))
                                break;
                            start = -1;
                        }
                    }
                }
            }
        }
    }
    for (const auto &entry : images) {
        if (isCancelled(cancel))
            return aborted();
        const auto &a = *entry.first;
        auto settings = d.layers[entry.second].toMap();
        if (entry.second == a.layer)
            for (auto it = a.settings.begin(); it != a.settings.end(); ++it)
                settings[it.key()] = it.value();
        QString error;
        if (!a.reliefAsset) {
            job.errors << "Bilddaten fehlen.";
            continue;
        }
        auto image = ImageProcessing::apply(a.reliefAsset->image, a.style, error, cancel);
        if (isCancelled(cancel))
            return aborted();
        if (!error.isEmpty()) {
            job.errors << error;
            continue;
        }
        job.previews.insert(a.id, image);
        if (settings["mode"] == "Schnitt") {
            job.errors << "Rasterbilder müssen graviert werden.";
            continue;
        }
        double interval = settings.value("interval", .1).toDouble(),
               feed = settings["speed"].toDouble(), power = settings["power"].toDouble();
        int passes = settings["passes"].toInt();
        if (!std::isfinite(interval) || interval < .02 || interval > 2 || feed < 1 || feed > 7000 ||
            power < 0 || power > 100 || passes < 1 || passes > 100) {
            job.errors << "Ungültige Rasterparameter.";
            continue;
        }
        int columns = std::ceil(a.bounds.width() / interval),
            rows = std::ceil(a.bounds.height() / interval);
        if (qint64(columns) * rows * passes > 20000000) {
            job.errors << "Rasterjob zu aufwändig.";
            continue;
        }
        for (int pass = 0; pass < passes && job.errors.isEmpty(); ++pass)
            for (int row = 0; row < rows && job.errors.isEmpty(); ++row) {
                if (isCancelled(cancel))
                    return aborted();
                bool reverse = row % 2;
                double y = std::min((row + .5) * interval, a.bounds.height() - 1e-8);
                int py =
                    std::clamp(image.height() - 1 - int(y / a.bounds.height() * image.height()), 0,
                               image.height() - 1);
                int start = -1;
                double previous = 0;
                for (int step = 0; step <= columns; ++step) {
                    int col = reverse ? columns - 1 - step : step;
                    double level = 0;
                    if (step < columns) {
                        int px = std::clamp(
                            int(std::min((col + .5) * interval, a.bounds.width() - 1e-8) /
                                a.bounds.width() * image.width()),
                            0, image.width() - 1);
                        auto c = image.pixelColor(px, py);
                        level = c.alpha() ? power * (1 - qGray(c.rgb()) / 255.) : 0;
                        level = std::round(level * 10) / 10;
                    }
                    if (level != previous || step == columns) {
                        if (start >= 0 && previous > 0) {
                            double x1 =
                                reverse ? std::min((columns - start) * interval, a.bounds.width())
                                        : start * interval;
                            double x2 = reverse ? (columns - step) * interval
                                                : std::min(step * interval, a.bounds.width());
                            if (!add({a.bounds.x() + x1, a.bounds.y() + y}, 3000, 0, entry.second,
                                     true) ||
                                !add({a.bounds.x() + x2, a.bounds.y() + y}, feed, previous,
                                     entry.second, false))
                                break;
                        }
                        start = step;
                        previous = level;
                    }
                }
            }
    }
    for (int shapeIndex = 0; shapeIndex < shapes.size(); ++shapeIndex) {
        if (isCancelled(cancel))
            return aborted();
        if (shapes[shapeIndex].settings.value("optimize", false).toBool()) {
            int best = shapeIndex;
            double distance = 1e99;
            for (int candidate = shapeIndex;
                 candidate < std::min(int(shapes.size()), shapeIndex + 32); ++candidate) {
                if (shapes[candidate].layer != shapes[shapeIndex].layer)
                    break;
                auto box = shapes[candidate].path.boundingRect();
                bool innerPending = false;
                if (shapes[candidate].settings["mode"] == "Schnitt")
                    for (int i = shapeIndex; i < shapes.size(); ++i)
                        if (i != candidate && shapes[i].layer == shapes[candidate].layer) {
                            auto inner = shapes[i].path.boundingRect();
                            if (box.contains(inner) && inner.width() * inner.height() <
                                                           box.width() * box.height() - .000001) {
                                innerPending = true;
                                break;
                            }
                        }
                if (innerPending)
                    continue;
                auto element = shapes[candidate].path.elementAt(0);
                double d = QLineF(head, {element.x, element.y}).length();
                if (d < distance) {
                    distance = d;
                    best = candidate;
                }
            }
            if (best != shapeIndex)
                std::swap(shapes[shapeIndex], shapes[best]);
        }
        const auto &shape = shapes[shapeIndex];
        const auto &settings = shape.settings;
        double speed = settings["speed"].toDouble(), power = settings["power"].toDouble();
        int passes = settings["passes"].toInt();
        if (!std::isfinite(speed) || speed < 1 || speed > 7000 || !std::isfinite(power) ||
            power < 0 || power > 100 || passes < 1 || passes > 100) {
            job.errors
                << "Ungültige Bearbeitungsparameter: v 1–7000 mm/min, P 0–100 %, 1–100 Durchgänge.";
            continue;
        }
        auto cutPath = shape.path;
        const double kerf = settings.value("kerf", 0).toDouble();
        if (settings["mode"] == "Schnitt" && std::abs(kerf) > .00001) {
            bool closed = true;
            for (const auto &poly : cutPath.toSubpathPolygons())
                if (poly.size() > 1 && QLineF(poly.first(), poly.last()).length() > .000001)
                    closed = false;
            if (!closed) {
                job.errors << "Schnittfugenkompensation benötigt geschlossene Konturen.";
                continue;
            }
            QPainterPathStroker stroker;
            stroker.setWidth(std::abs(kerf));
            stroker.setJoinStyle(Qt::RoundJoin);
            auto rim = stroker.createStroke(cutPath);
            cutPath = kerf > 0 ? cutPath.united(rim) : cutPath.subtracted(rim);
        }
        for (int pass = 0; pass < passes && job.errors.isEmpty(); ++pass) {
            for (int direction = 0; direction < (settings.value("crosshatch", false).toBool() &&
                                                         settings["mode"] != "Schnitt"
                                                     ? 2
                                                     : 1);
                 ++direction) {
                double angle = settings.value("angle", 0).toDouble() +
                               pass * settings.value("angleIncrement", 0).toDouble() +
                               direction * 90.;
                QTransform scan;
                scan.rotate(-angle);
                auto scanPath = scan.map(cutPath);
                auto unscan = scan.inverted();
                if (settings["mode"] == "Schnitt") {
                    scan = QTransform();
                    unscan = QTransform();
                    scanPath = cutPath;
                }
                auto polygons = scanPath.toSubpathPolygons(QTransform::fromScale(50, 50));
                for (auto &poly : polygons)
                    for (auto &point : poly)
                        point /= 50.;
                if (settings["mode"] == "Schnitt")
                    std::stable_sort(polygons.begin(), polygons.end(),
                                     [](const auto &a, const auto &b) {
                                         auto aa = a.boundingRect(), bb = b.boundingRect();
                                         return aa.width() * aa.height() < bb.width() * bb.height();
                                     });
                if (settings["mode"] == "Schnitt") {
                    for (auto poly : polygons) {
                        if (poly.size() < 2)
                            continue;
                        bool closed = QLineF(poly.first(), poly.last()).length() < .00001;
                        if (closed && poly.size() > 3) {
                            poly.removeLast();
                            int start =
                                std::clamp(qRound(settings.value("startPoint", 0).toDouble() / 100 *
                                                  poly.size()),
                                           0, int(poly.size() - 1));
                            if (settings.value("optimize", false).toBool()) {
                                double distance = 1e99;
                                for (int i = 0; i < poly.size(); ++i) {
                                    double d = QLineF(head, poly[i]).length();
                                    if (d < distance) {
                                        distance = d;
                                        start = i;
                                    }
                                }
                            }
                            std::rotate(poly.begin(), poly.begin() + start, poly.end());
                            poly.append(poly.first());
                        }
                        double perimeter = 0;
                        for (int i = 1; i < poly.size(); ++i)
                            perimeter += QLineF(poly[i - 1], poly[i]).length();
                        int tabs = closed ? settings.value("tabs", 0).toInt() : 0;
                        double tabWidth = settings.value("tabWidth", 1).toDouble();
                        double leadIn = settings.value("leadIn", 0).toDouble(),
                               leadOut = settings.value("leadOut", 0).toDouble();
                        auto first = poly.first();
                        auto tangent = poly[1] - first;
                        double norm = std::hypot(tangent.x(), tangent.y());
                        if (norm < 1e-9)
                            continue;
                        tangent /= norm;
                        if (!add(first - tangent * leadIn, 3000, 0, shape.layer, true) ||
                            !add(first, speed, power, shape.layer, false))
                            break;
                        double walked = 0;
                        for (int i = 1; i < poly.size(); ++i) {
                            auto from = poly[i - 1], to = poly[i];
                            double length = QLineF(from, to).length();
                            if (length < 1e-9)
                                continue;
                            QList<double> splits{0, length};
                            for (int tab = 0; tab < tabs; ++tab) {
                                double center = (tab + .5) * perimeter / tabs;
                                for (double edge : {center - tabWidth / 2, center + tabWidth / 2})
                                    if (edge > walked && edge < walked + length)
                                        splits.append(edge - walked);
                            }
                            std::sort(splits.begin(), splits.end());
                            for (int k = 1; k < splits.size(); ++k) {
                                double mid = walked + (splits[k - 1] + splits[k]) / 2;
                                bool bridge = false;
                                for (int tab = 0; tab < tabs; ++tab)
                                    if (std::abs(mid - (tab + .5) * perimeter / tabs) <
                                        tabWidth / 2)
                                        bridge = true;
                                if (!add(from + (to - from) * (splits[k] / length), speed,
                                         bridge ? 0 : power, shape.layer, false))
                                    break;
                            }
                            walked += length;
                            if (!job.errors.isEmpty())
                                break;
                        }
                        if (leadOut > 0) {
                            auto delta = poly.last() - poly[poly.size() - 2];
                            double n = std::hypot(delta.x(), delta.y());
                            if (n > 1e-9)
                                add(poly.last() + delta / n * leadOut, speed, power, shape.layer,
                                    false);
                        }
                    }
                } else {
                    const double interval = settings.value("interval", .1).toDouble();
                    if (!std::isfinite(interval) || interval < .001 || interval > 10) {
                        job.errors << "Ungültiges Gravurintervall.";
                        break;
                    }
                    const auto box = scanPath.boundingRect();
                    const int rowCount = std::max(0, int(std::ceil(box.height() / interval - .5)));
                    QVector<QList<QPair<double, int>>> rows(rowCount);
                    int crossings = 0;
                    for (const auto &poly : polygons)
                        for (int i = 0; i < poly.size() && job.errors.isEmpty(); ++i) {
                            auto a = poly[i], b = poly[(i + 1) % poly.size()];
                            if (a.y() == b.y())
                                continue;
                            int first = std::max(
                                0, int(std::ceil((std::min(a.y(), b.y()) - box.top()) / interval -
                                                 .5)));
                            int last = std::min(
                                rowCount,
                                int(std::ceil((std::max(a.y(), b.y()) - box.top()) / interval -
                                              .5)));
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
                        if (isCancelled(cancel))
                            return aborted();
                        const double y = box.top() + (row + .5) * interval;
                        auto &hits = rows[row];
                        std::sort(hits.begin(), hits.end(),
                                  [](auto a, auto b) { return a.first < b.first; });
                        QList<QPair<double, double>> spans;
                        int winding = 0;
                        double start = 0;
                        for (auto hit : hits) {
                            bool before = scanPath.fillRule() == Qt::OddEvenFill
                                              ? (winding % 2 != 0)
                                              : (winding != 0);
                            winding += scanPath.fillRule() == Qt::OddEvenFill ? 1 : hit.second;
                            bool after = scanPath.fillRule() == Qt::OddEvenFill ? (winding % 2 != 0)
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
                            const double overscan = settings.value("overscan", 0).toDouble();
                            const double offset =
                                reverse ? settings.value("scanOffset", 0).toDouble() : 0;
                            a.rx() += offset;
                            b.rx() += offset;
                            auto delta = b - a;
                            delta /= std::max(.000001, QLineF(a, b).length());
                            if (!add(unscan.map(a - delta * overscan), 3000, 0, shape.layer,
                                     true) ||
                                !add(unscan.map(a), speed, 0, shape.layer, false) ||
                                !add(unscan.map(b), speed, power, shape.layer, false) ||
                                !add(unscan.map(b + delta * overscan), speed, 0, shape.layer,
                                     false))
                                break;
                        }
                        reverse = !reverse;
                    }
                }
            }
        }
    }
    if (isCancelled(cancel))
        return aborted();
    if (job.moves.isEmpty() && job.errors.isEmpty())
        job.errors << "Keine aktiven Laserpfade vorhanden.";
    if (!job.errors.isEmpty()) {
        job.moves.clear();
        job.workLength = job.travelLength = job.seconds = job.minimumSeconds = 0;
    }
    return job;
}
QStringList JobPlan::gcode(const Cancellation &cancel) const {
    if (isCancelled(cancel))
        return {};
    if (valid() && !commands.isEmpty())
        return commands;
    if (!valid())
        return {};
    QStringList lines{"M5",  "(Blazer Studio: GRBL laser mode, $30=1000, $32=1, G54)",
                      "G21", "G90",
                      "G54", "G17",
                      "G94", "S0"};
    bool emitting = false;
    double lastPower = -1, lastFeed = -1;
    int lastSlice = 0;
    for (const auto &m : moves) {
        if (isCancelled(cancel))
            return {};
        if (m.slice != 0 && m.slice != lastSlice) {
            lines << (m.slice > 0 ? "(Relief slice " : "(Relief cleanup after ") +
                         QString::number(std::abs(m.slice)) + ")";
            lastSlice = m.slice;
        }
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
            {"seconds", seconds},
            {"minimumSeconds", minimumSeconds}};
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
