#include "plotimport.h"
#include "geometry.h"
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace {
constexpr int elementLimit = 100000;
constexpr double unit = 0.025; // HP plotter unit, in millimetres.
struct Mapping {
    QPointF anchor, origin;
    double x = 1, y = 1;
    QPointF map(QPointF p) const {
        return {anchor.x() + (p.x() - origin.x()) * x, anchor.y() + (p.y() - origin.y()) * y};
    }
    QPointF inverse(QPointF p) const {
        return {origin.x() + (p.x() - anchor.x()) / x, origin.y() + (p.y() - anchor.y()) / y};
    }
};

class Hpgl {
    QString text;
    qsizetype offset = 0;
    QList<QPainterPath> paths;
    QPainterPath stroke;
    QPointF position;
    QPointF physicalPosition;
    QPointF p1;
    std::optional<QPointF> p2;
    QList<double> scaling;
    Mapping mapping;
    bool absolute = true, down = false, selected = false;
    int elements = 0, commands = 0, work = 0;
    QString error;

    bool fail(const QString &message) {
        error = message;
        return false;
    }
    bool plotterPoint(QPointF p) {
        return (std::isfinite(p.x()) && std::isfinite(p.y()) &&
                std::abs(p.x() * unit) <= Geometry::coordinateLimit &&
                std::abs(p.y() * unit) <= Geometry::coordinateLimit) ||
               fail("Koordinate außerhalb des unterstützten Bereichs (±2000 mm).");
    }
    bool point(QPointF p) { return plotterPoint(mapping.map(p)); }
    bool updateMapping() {
        Mapping next;
        if (!scaling.isEmpty()) {
            const int type = scaling.size() == 4 ? 0 : int(scaling[4]);
            next.anchor = p1;
            next.origin = {scaling[0], scaling[2]};
            if (type == 2) {
                next.x = scaling[1];
                next.y = scaling[3];
            } else {
                if (!p2)
                    return fail(
                        "SC-Typ 0/1 benötigt explizite P1/P2 aus IP; Seitengröße unbekannt.");
                next.x = (p2->x() - p1.x()) / (scaling[1] - scaling[0]);
                next.y = (p2->y() - p1.y()) / (scaling[3] - scaling[2]);
                if (type == 1) {
                    const double magnitude = std::min(std::abs(next.x), std::abs(next.y));
                    next.x = std::copysign(magnitude, next.x);
                    next.y = std::copysign(magnitude, next.y);
                    const double left = scaling.size() == 7 ? scaling[5] / 100 : .5;
                    const double bottom = scaling.size() == 7 ? scaling[6] / 100 : .5;
                    // Unused margins follow the P1-to-P2 direction. Reversing
                    // the reference points also reverses margin placement.
                    next.anchor +=
                        QPointF((p2->x() - p1.x() - (scaling[1] - scaling[0]) * next.x) * left,
                                (p2->y() - p1.y() - (scaling[3] - scaling[2]) * next.y) * bottom);
                }
            }
        }
        if (!std::isfinite(next.x) || !std::isfinite(next.y) || next.x == 0 || next.y == 0)
            return fail("Entartete HPGL-Skalierung.");
        const auto physical = physicalPosition;
        const auto moved = next.inverse(physical);
        const auto check = next.map(moved) - physical;
        if (!std::isfinite(moved.x()) || !std::isfinite(moved.y()) || !std::isfinite(check.x()) ||
            !std::isfinite(check.y()) || std::hypot(check.x(), check.y()) * unit > .000001)
            return fail("HPGL-Skalierung kann die Stiftposition nicht genau erhalten.");
        mapping = next;
        position = moved; // Coordinate changes never move the physical pen.
        return true;
    }
    bool flush() {
        if (stroke.elementCount() > 1) {
            if (paths.size() >= Geometry::objectLimit)
                return fail("Zu viele HPGL-Pfade.");
            paths.append(stroke);
        }
        stroke = {};
        return true;
    }
    void beginStroke() {
        if (stroke.isEmpty()) {
            stroke.moveTo(physicalPosition * unit);
            ++elements;
        }
    }
    bool line(QPointF target) {
        if (!point(target))
            return false;
        if (down && selected && target != position) {
            beginStroke();
            if (++elements > elementLimit)
                return fail("Zu viele HPGL-Pfadelemente.");
            stroke.lineTo(mapping.map(target) * unit);
        }
        position = target;
        physicalPosition = mapping.map(target);
        return true;
    }
    bool numbers(QList<double> &values) {
        bool comma = false;
        while (offset < text.size()) {
            while (offset < text.size() && text[offset].isSpace())
                ++offset;
            if (offset == text.size() || text[offset] == ';' || text[offset].isLetter()) {
                if (comma)
                    return fail("Fehlende Zahl nach Komma.");
                if (offset < text.size() && text[offset] == ';')
                    ++offset;
                return true;
            }
            const auto start = offset;
            if (text[offset] == '+' || text[offset] == '-')
                ++offset;
            const auto digit = [&] {
                return offset < text.size() && text[offset] >= '0' && text[offset] <= '9';
            };
            bool digits = false;
            while (digit()) {
                digits = true;
                ++offset;
            }
            if (offset < text.size() && text[offset] == '.') {
                ++offset;
                while (digit()) {
                    digits = true;
                    ++offset;
                }
            }
            if (!digits || offset - start > 64)
                return fail("Ungültige HPGL-Zahl.");
            bool ok = false;
            double value = text.mid(start, offset - start).toDouble(&ok);
            if (!ok || !std::isfinite(value) || std::abs(value) > 1e9)
                return fail("HPGL-Zahl außerhalb des unterstützten Bereichs.");
            values.append(value);
            if (++work > elementLimit * 2)
                return fail("Zu viele HPGL-Parameter.");
            comma = false;
            bool space = false;
            while (offset < text.size() && text[offset].isSpace()) {
                ++offset;
                space = true;
            }
            if (offset < text.size() && text[offset] == ',') {
                comma = true;
                ++offset;
            } else if (offset < text.size() && text[offset] != ';' && !text[offset].isLetter() &&
                       !space && text[offset] != '+' && text[offset] != '-') {
                return fail("Fehlendes Trennzeichen zwischen HPGL-Zahlen.");
            }
        }
        return !comma || fail("Fehlende Zahl nach Komma.");
    }
    bool arc(QPointF center, double sweep, double chord) {
        if (!point(center) || !std::isfinite(sweep) || std::abs(sweep) > 32767 || chord < 0.5 ||
            chord > 180)
            return fail("Ungültiger HPGL-Bogen oder Sehnenwinkel.");
        const QPointF radius = position - center;
        const double start = std::atan2(radius.y(), radius.x());
        const double length = std::hypot(radius.x(), radius.y());
        if (length == 0 || sweep == 0)
            return true;
        const int steps = int(std::ceil(std::abs(sweep) / chord));
        work += steps;
        if (work > elementLimit * 2)
            return fail("HPGL-Bogen überschreitet das Elementlimit.");
        for (int i = 1; i <= steps; ++i) {
            // Keep the requested chord angle; only the final chord may be shorter.
            const double angle =
                start +
                std::copysign(std::min(i * chord, std::abs(sweep)), sweep) * std::numbers::pi / 180;
            if (!line(center + QPointF(std::cos(angle), std::sin(angle)) * length))
                return false;
        }
        return true;
    }
    bool command(const QString &op, const QList<double> &v) {
        if (op == "IN") {
            if (!v.isEmpty() || !flush())
                return false;
            position = {};
            physicalPosition = {};
            p1 = {};
            p2.reset();
            scaling.clear();
            mapping = {};
            absolute = true;
            down = selected = false;
            return true;
        }
        if (op == "IP") {
            if (v.size() != 0 && v.size() != 2 && v.size() != 4)
                return fail("IP benötigt null, zwei oder vier Koordinaten.");
            const QPointF nextP1 =
                v.isEmpty() ? QPointF() : QPointF(std::round(v[0]), std::round(v[1]));
            if (!plotterPoint(nextP1))
                return false;
            if (v.isEmpty())
                p2.reset(); // The default frame size is not encoded in this file.
            else if (v.size() == 4) {
                p2 = QPointF(std::round(v[2]), std::round(v[3]));
                if (p2->x() == nextP1.x())
                    p2->rx() += 1;
                if (p2->y() == nextP1.y())
                    p2->ry() += 1;
            } else if (p2)
                *p2 += nextP1 - p1; // P2 tracks a P1-only change.
            if (p2 && !plotterPoint(*p2))
                return false;
            p1 = nextP1;
            return updateMapping();
        }
        if (op == "SC") {
            if (!v.isEmpty()) {
                if (v.size() != 4 && v.size() != 5 && v.size() != 7)
                    return fail("Ungültige SC-Parameterzahl.");
                const double type = v.size() == 4 ? 0 : std::round(v[4]);
                if (type < 0 || type > 2 || (type == 2 && v.size() != 5))
                    return fail("SC-Typ muss 0, 1 oder 2 sein.");
                if ((type < 2 && (v[0] == v[1] || v[2] == v[3])) ||
                    (type == 2 && (v[1] == 0 || v[3] == 0)))
                    return fail("Entarteter SC-Bereich oder Faktor null.");
                if (type == 1 && v.size() == 7 &&
                    (v[5] < 0 || v[5] > 100 || v[6] < 0 || v[6] > 100))
                    return fail("SC-Ränder müssen zwischen 0 und 100 Prozent liegen.");
            }
            scaling = v;
            if (scaling.size() > 4)
                scaling[4] = std::round(scaling[4]);
            return updateMapping();
        }
        if (op == "SP") {
            if (v.size() > 1 ||
                (!v.isEmpty() && (v[0] < 0 || v[0] > 255 || std::floor(v[0]) != v[0])))
                return fail("Ungültige HPGL-Stiftauswahl.");
            if (!flush())
                return false;
            selected = !v.isEmpty() && v[0] > 0;
            return true;
        }
        if (op == "PA" || op == "PR" || op == "PU" || op == "PD") {
            if (v.size() % 2)
                return fail("HPGL-Koordinaten benötigen vollständige X/Y-Paare.");
            if (op == "PA" || op == "PR")
                absolute = op == "PA";
            else {
                down = op == "PD";
                if (!down && !flush())
                    return false;
            }
            for (int i = 0; i < v.size(); i += 2)
                if (!line(QPointF(v[i], v[i + 1]) + (absolute ? QPointF() : position)))
                    return false;
            return true;
        }
        if (op == "AA" || op == "AR") {
            if (v.size() != 3 && v.size() != 4)
                return fail("HPGL-Bogen benötigt Zentrum, Winkel und optional Sehnenwinkel.");
            return arc(QPointF(v[0], v[1]) + (op == "AR" ? position : QPointF()), v[2],
                       v.size() == 4 ? v[3] : 5);
        }
        if (op == "CI") {
            if ((v.size() != 1 && v.size() != 2) || v[0] <= 0)
                return fail("HPGL-Kreis benötigt einen positiven Radius.");
            const double chord = v.size() == 2 ? v[1] : 5;
            if (!flush())
                return false;
            const QPointF center = position;
            const QPointF centerPhysical = physicalPosition;
            const bool previousDown = down;
            down = false;
            if (!line(center + QPointF(v[0], 0)))
                return false;
            down = true; // CI draws even when the current pen is up.
            if (!arc(center, 360, chord))
                return false;
            if (!stroke.isEmpty()) {
                const int previousElements = stroke.elementCount();
                stroke.closeSubpath();
                elements += stroke.elementCount() - previousElements;
                if (elements > elementLimit)
                    return fail("Zu viele HPGL-Pfadelemente.");
            }
            if (!flush())
                return false;
            position = center;
            physicalPosition = centerPhysical;
            down = previousDown;
            return true;
        }
        if (op == "BZ" || op == "BR") {
            if (v.isEmpty() || v.size() % 6)
                return fail("HPGL-Bézierkurve benötigt drei Kontrollpunktpaare je Segment.");
            for (int i = 0; i < v.size(); i += 6) {
                const QPointF origin = op == "BR" ? position : QPointF();
                const QPointF a = origin + QPointF(v[i], v[i + 1]);
                const QPointF b = origin + QPointF(v[i + 2], v[i + 3]);
                const QPointF end = origin + QPointF(v[i + 4], v[i + 5]);
                if (!point(a) || !point(b) || !point(end))
                    return false;
                if (down && selected) {
                    beginStroke();
                    elements += 3;
                    if (elements > elementLimit)
                        return fail("Zu viele HPGL-Pfadelemente.");
                    stroke.cubicTo(mapping.map(a) * unit, mapping.map(b) * unit,
                                   mapping.map(end) * unit);
                }
                position = end;
                physicalPosition = mapping.map(end);
            }
            return true;
        }
        return fail("HPGL-Befehl " + op + " wird nicht unterstützt; kein Teilimport.");
    }

  public:
    explicit Hpgl(const QByteArray &bytes) : text(QString::fromLatin1(bytes)) {}
    PlotImport::Result run() {
        while (offset < text.size()) {
            while (offset < text.size() && (text[offset].isSpace() || text[offset] == ';'))
                ++offset;
            if (offset == text.size())
                break;
            if (++commands > elementLimit || offset + 1 >= text.size() ||
                !text[offset].isLetter() || !text[offset + 1].isLetter()) {
                error = "Ungültige HPGL-Befehlsfolge oder zu viele Befehle.";
                break;
            }
            const QString op = text.mid(offset, 2).toUpper();
            offset += 2;
            QList<double> values;
            if (!numbers(values) || !command(op, values)) {
                if (error.isEmpty())
                    error = "Ungültige Parameter für HPGL-Befehl " + op + ".";
                break;
            }
        }
        if (!error.isEmpty() || !flush())
            return {{}, error};
        if (paths.isEmpty())
            return {{}, "Keine gezeichneten HPGL-Pfade (Stift auswählen und absenken)."};
        QRectF bounds;
        bool first = true;
        for (auto &path : paths) {
            const auto rect = path.boundingRect();
            if (first)
                bounds = rect;
            else
                bounds = QRectF(QPointF(std::min(bounds.left(), rect.left()),
                                        std::min(bounds.top(), rect.top())),
                                QPointF(std::max(bounds.right(), rect.right()),
                                        std::max(bounds.bottom(), rect.bottom())));
            first = false;
        }
        if (bounds.width() > Geometry::coordinateLimit ||
            bounds.height() > Geometry::coordinateLimit)
            return {{}, "HPGL-Zeichnung größer als 2000 mm."};
        QTransform place;
        place.translate(-bounds.left(), -bounds.top());
        for (auto &path : paths)
            path = place.map(path);
        return {paths, {}};
    }
};
} // namespace

PlotImport::Result PlotImport::hpgl(const QByteArray &bytes) {
    if (bytes.size() > 16 * 1024 * 1024)
        return {{}, "HPGL-Datei größer als 16 MiB."};
    return Hpgl(bytes).run();
}
