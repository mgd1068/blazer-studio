#include "plotimport.h"
#include "geometry.h"
#include <QTransform>
#include <cmath>
#include <numbers>

namespace {
constexpr int elementLimit = 100000;
constexpr double unit = 0.025; // HP plotter unit, in millimetres.

class Hpgl {
    QString text;
    qsizetype offset = 0;
    QList<QPainterPath> paths;
    QPainterPath stroke;
    QPointF position;
    bool absolute = true, down = false, selected = false;
    int elements = 0, commands = 0, work = 0;
    QString error;

    bool fail(const QString &message) {
        error = message;
        return false;
    }
    bool point(QPointF p) {
        return (std::isfinite(p.x()) && std::isfinite(p.y()) &&
                std::abs(p.x() * unit) <= Geometry::coordinateLimit &&
                std::abs(p.y() * unit) <= Geometry::coordinateLimit) ||
               fail("Koordinate außerhalb des unterstützten Bereichs (±2000 mm).");
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
            stroke.moveTo(position * unit);
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
            stroke.lineTo(target * unit);
        }
        position = target;
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
                       !space) {
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
            absolute = true;
            down = selected = false;
            return true;
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
                    stroke.cubicTo(a * unit, b * unit, end * unit);
                }
                position = end;
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
