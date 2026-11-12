#include "geometry.h"
#include "plotimport.h"
#include <QLineF>
#include <QTransform>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>

namespace {
constexpr int elementLimit = 100000;
constexpr double tau = 2 * std::numbers::pi;
struct Word {
    char letter;
    double value;
};
struct Block {
    int line;
    QList<Word> words;
};

class Gcode {
    const QByteArray &input;
    QList<Block> blocks;
    QList<QPainterPath> paths;
    QPainterPath stroke;
    QString error;
    int line = 0, wordCount = 0, elements = 0, arcSteps = 0;
    bool spindleAware = false, spindle = false, absolute = true, arcAbsolute = false;
    bool knownX = false, knownY = false;
    bool floatingOrigin = false, floatingDrawn = false;
    double unit = 1, power = 0;
    int motion = 0;
    QPointF position;

    bool fail(const QString &message) {
        error = "GCode, Zeile " + QString::number(line) + ": " + message + "; kein Teilimport.";
        return false;
    }
    bool coordinate(double v) {
        return (std::isfinite(v) && std::abs(v) <= Geometry::coordinateLimit) ||
               fail("Koordinate außerhalb ±2000 mm");
    }
    bool point(QPointF p) { return coordinate(p.x()) && coordinate(p.y()); }
    bool flush() {
        if (stroke.elementCount() > 1) {
            if (paths.size() >= elementLimit)
                return fail("Zu viele Pfade");
            paths.append(stroke);
        }
        stroke = {};
        return true;
    }
    bool drawTo(QPointF target, bool draw) {
        if (!point(target))
            return false;
        if (!draw)
            return flush();
        if (target == position)
            return true;
        if (stroke.isEmpty()) {
            stroke.moveTo(position);
            ++elements;
        }
        if (++elements > elementLimit)
            return fail("Zu viele Pfadelemente");
        stroke.lineTo(target);
        return true;
    }
    bool lex() {
        bool started = false, wrapped = false, closed = false, ended = false;
        qsizetype offset = input.startsWith("\xEF\xBB\xBF") ? 3 : 0;
        while (offset < input.size()) {
            if (ended && !wrapped)
                break;
            const auto start = offset;
            while (offset < input.size() && input[offset] != '\r' && input[offset] != '\n')
                ++offset;
            const auto length = offset - start;
            if (offset < input.size()) {
                const char end = input[offset++];
                if (end == '\r' && offset < input.size() && input[offset] == '\n')
                    ++offset;
            }
            if (++line > 100000 || length > 4096)
                return fail("Zu viele Zeilen oder Zeile länger als 4096 Byte");
            // End-of-program data is not interpreted. A percent wrapper still
            // needs its closing delimiter after M2/M30.
            if (ended) {
                const auto tail = input.mid(start, length).trimmed();
                if (tail == "%") {
                    closed = true;
                    break;
                }
                continue;
            }
            QByteArray code;
            bool comment = false;
            for (qsizetype i = start; i < start + length; ++i) {
                const char c = input[i];
                if (c == ';' && !comment)
                    break;
                if (c == '(') {
                    if (comment)
                        return fail("Verschachtelter Kommentar");
                    comment = true;
                } else if (c == ')') {
                    if (!comment)
                        return fail("Kommentarende ohne Anfang");
                    comment = false;
                } else if (!comment && c != ' ' && c != '\t') {
                    if (c < 33 || c > 126)
                        return fail("Ungültiges Zeichen außerhalb eines Kommentars");
                    code += c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c;
                }
            }
            if (comment)
                return fail("Nicht geschlossener Kommentar");
            if (code.isEmpty())
                continue;
            if (code == "%") {
                if (!started) {
                    started = wrapped = true;
                    continue;
                }
                if (!wrapped)
                    return fail("Prozent-Ende ohne Programmrahmen");
                closed = true;
                break;
            }
            if (ended)
                continue;
            started = true;
            Block block{line, {}};
            qsizetype i = 0;
            while (i < code.size()) {
                const char letter = code[i++];
                if (letter < 'A' || letter > 'Z')
                    return fail("Erwartet wird ein Buchstabe mit Dezimalzahl");
                const auto numberStart = i;
                if (i < code.size() && (code[i] == '+' || code[i] == '-'))
                    ++i;
                bool digits = false;
                while (i < code.size() && code[i] >= '0' && code[i] <= '9') {
                    digits = true;
                    ++i;
                }
                if (i < code.size() && code[i] == '.') {
                    ++i;
                    while (i < code.size() && code[i] >= '0' && code[i] <= '9') {
                        digits = true;
                        ++i;
                    }
                }
                if (!digits || i - numberStart > 64)
                    return fail("Ungültige Dezimalzahl");
                bool ok;
                const double value = code.mid(numberStart, i - numberStart).toDouble(&ok);
                if (!ok || !std::isfinite(value) || std::abs(value) > 1e9 || ++wordCount > 500000)
                    return fail("Zahl oder Wortanzahl überschreitet das Limit");
                block.words.append({letter, value});
                if (letter == 'S' || (letter == 'M' && (value == 3 || value == 4 || value == 5)))
                    spindleAware = true;
                if (letter == 'M' && (value == 2 || value == 30))
                    ended = true;
            }
            blocks.append(block);
        }
        return !wrapped || closed || fail("Nicht geschlossener Prozent-Programmrahmen");
    }
    double sweepFor(QPointF center, QPointF target, bool clockwise) const {
        double sweep = std::atan2(target.y() - center.y(), target.x() - center.x()) -
                       std::atan2(position.y() - center.y(), position.x() - center.x());
        if (clockwise) {
            if (sweep >= 0)
                sweep -= tau;
        } else if (sweep <= 0)
            sweep += tau;
        return sweep;
    }
    bool arc(QPointF target, const std::array<std::optional<double>, 26> &v, bool draw) {
        const auto has = [&](char c) { return v[c - 'A'].has_value(); };
        const auto value = [&](char c) { return v[c - 'A'].value_or(0) * unit; };
        const bool clockwise = motion == 2;
        QPointF center;
        if (has('R')) {
            if (has('I') || has('J'))
                return fail("Radius und Zentrum gleichzeitig angegeben");
            const QPointF delta = target - position;
            const double distance = std::hypot(delta.x(), delta.y());
            const double radius = std::abs(value('R'));
            if (distance == 0 || radius == 0 || distance > 2 * radius + 1e-9)
                return fail("Ungültiger Radiusbogen");
            const QPointF middle = (position + target) / 2;
            const QPointF normal(-delta.y() / distance, delta.x() / distance);
            const double height =
                std::sqrt(std::max(0., radius * radius - distance * distance / 4));
            center = middle + normal * height;
            const bool longArc = value('R') < 0;
            if ((std::abs(sweepFor(center, target, clockwise)) > std::numbers::pi + 1e-10) !=
                longArc)
                center = middle - normal * height;
        } else {
            if ((!has('I') && !has('J')) || (arcAbsolute && (!has('I') || !has('J'))))
                return fail("Bogen benötigt ein vollständiges Zentrum");
            center = QPointF(value('I'), value('J')) + (arcAbsolute ? QPointF() : position);
        }
        if (!point(center))
            return false;
        const double radius = QLineF(position, center).length();
        if (radius == 0 || std::abs(radius - QLineF(target, center).length()) > .005)
            return fail("Start-/Endradius des Bogens unterscheidet sich um mehr als 0,005 mm");
        const double turns = v['P' - 'A'].value_or(1);
        if (turns < 1 || turns > 1000 || std::floor(turns) != turns)
            return fail("Bogen-P muss eine ganze Umlaufzahl von 1 bis 1000 sein");
        double sweep = sweepFor(center, target, clockwise);
        sweep += (clockwise ? -1 : 1) * (turns - 1) * tau;
        const double step =
            std::min(std::numbers::pi / 18, 2 * std::acos(std::clamp(1 - .01 / radius, -1., 1.)));
        if (!(step > 0))
            return fail("Bogenradius außerhalb des unterstützten Bereichs");
        const double requested = std::ceil(std::abs(sweep) / step);
        if (requested > elementLimit - arcSteps)
            return fail("Zu viele Bogen-Unterteilungsschritte");
        const int steps = int(requested);
        arcSteps += steps;
        const double start = std::atan2(position.y() - center.y(), position.x() - center.x());
        for (int i = 1; i <= steps; ++i) {
            const double angle = start + sweep * i / steps;
            const QPointF point =
                i == steps ? target : center + QPointF(std::cos(angle), std::sin(angle)) * radius;
            if (!drawTo(point, draw))
                return false;
            position = point;
        }
        return true;
    }
    bool block(const Block &block) {
        line = block.line;
        std::array<std::optional<double>, 26> values{};
        std::array<bool, 11> groups{};
        std::array<bool, 3> mGroups{};
        bool dwell = false, explicitMotion = false;
        const auto group = [&](int index) {
            if (groups[index])
                return fail("Mehrere G-Befehle derselben Modalgruppe");
            groups[index] = true;
            return true;
        };
        for (qsizetype i = 0; i < block.words.size(); ++i) {
            const auto w = block.words[i];
            if (w.letter == 'G') {
                const auto code = qRound64(w.value * 10);
                if (std::abs(w.value - code / 10.) > 1e-8)
                    return fail("Nicht unterstützter G-Befehl");
                if (code == 0 || code == 10 || code == 20 || code == 30 || code == 800) {
                    if (!group(0))
                        return false;
                    motion = code == 800 ? -1 : int(code / 10);
                    explicitMotion = true;
                } else if (code == 200 || code == 210) {
                    if (!group(1))
                        return false;
                    unit = code == 200 ? 25.4 : 1;
                } else if (code == 900 || code == 910) {
                    if (!group(2))
                        return false;
                    absolute = code == 900;
                } else if (code == 901 || code == 911) {
                    if (!group(3))
                        return false;
                    arcAbsolute = code == 901;
                } else if (code == 170 || code == 400 || code == 490 || code == 540 ||
                           code == 940) {
                    const int index = code == 170   ? 4
                                      : code == 400 ? 5
                                      : code == 490 ? 6
                                      : code == 540 ? 7
                                                    : 8;
                    if (!group(index))
                        return false;
                } else if (code == 40) {
                    if (!group(9))
                        return false;
                    dwell = true;
                } else
                    return fail("G" + QString::number(w.value) + " wird nicht unterstützt");
            } else if (w.letter == 'M') {
                if (std::floor(w.value) != w.value || w.value < 0 || w.value > 999)
                    return fail("Ungültiger M-Befehl");
                const int code = int(w.value);
                const int index = code >= 3 && code <= 5                                ? 0
                                  : (code == 0 || code == 1 || code == 2 || code == 30) ? 1
                                  : (code == 7 || code == 8 || code == 9)               ? 2
                                                                                        : -1;
                if (index < 0)
                    return fail("M" + QString::number(code) + " wird nicht unterstützt");
                if (mGroups[index])
                    return fail("Mehrere M-Befehle derselben Modalgruppe");
                mGroups[index] = true;
                if (index == 0)
                    spindle = code != 5;
            } else {
                if (!QByteArray("NXYIJRPFST").contains(w.letter))
                    return fail(QString(w.letter) + " wird nicht unterstützt");
                auto &value = values[w.letter - 'A'];
                if (value.has_value())
                    return fail("Doppeltes Wort " + QString(w.letter));
                if (w.letter == 'N' &&
                    (i != 0 || w.value < 0 || w.value > 9999999 || std::floor(w.value) != w.value))
                    return fail("Ungültige Zeilennummer");
                value = w.value;
            }
        }
        const auto has = [&](char c) { return values[c - 'A'].has_value(); };
        const auto value = [&](char c) { return values[c - 'A'].value_or(0); };
        if (has('T') &&
            (value('T') < 0 || value('T') > 255 || std::floor(value('T')) != value('T')))
            return fail("Werkzeugnummer muss ganzzahlig zwischen 0 und 255 liegen");
        if (has('F') && value('F') <= 0)
            return fail("Vorschub muss positiv sein");
        if (has('S')) {
            if (value('S') < 0)
                return fail("Negative Spindelleistung");
            power = value('S');
        }
        const bool axes = has('X') || has('Y');
        const bool arcData = has('I') || has('J') || has('R');
        if (dwell) {
            if (!has('P') || value('P') < 0 || axes || arcData || explicitMotion)
                return fail("G4 benötigt P ohne Bewegungswörter");
            return true;
        }
        if (has('P') && (motion < 2 || !arcData))
            return fail("P außerhalb eines Bogens oder G4");
        if (arcData && motion < 2)
            return fail("Bogendaten ohne G2/G3");
        if (!axes && !arcData)
            return (motion < 2 || !explicitMotion) || fail("G2/G3 ohne Bogendaten");
        if (motion < 0)
            return fail("Koordinaten nach G80 ohne neuen Bewegungsmodus");
        const bool draw = motion != 0 && (!spindleAware || (spindle && power > 0));
        if (!absolute && !knownX && !knownY) {
            // A wholly relative drawing is defined up to translation. A virtual
            // origin preserves its geometry without consulting the controller.
            knownX = knownY = true;
            floatingOrigin = true;
        }
        if (absolute && axes && floatingOrigin) {
            if (floatingDrawn || draw || !has('X') || !has('Y') || motion >= 2)
                return fail("Relative Geometrie mit unbekanntem Ursprung kann nicht mit absoluten "
                            "Wegen kombiniert werden");
            floatingOrigin = false;
        }
        const bool wasKnown = knownX && knownY;
        QPointF target = position;
        if (!absolute && ((has('X') && !knownX) || (has('Y') && !knownY)))
            return fail("Relative Bewegung ohne bekannte Anfangsposition");
        if (has('X')) {
            target.setX(value('X') * unit + (absolute ? 0 : position.x()));
            knownX = true;
        }
        if (has('Y')) {
            target.setY(value('Y') * unit + (absolute ? 0 : position.y()));
            knownY = true;
        }
        if (!point(target))
            return false;
        if ((draw || motion >= 2) && (!wasKnown || !knownX || !knownY))
            return fail("Zeichenbewegung benötigt bekannte XY-Anfangsposition, etwa G0 X… Y…");
        if (floatingOrigin && draw && (target != position || arcData))
            floatingDrawn = true;
        if (motion >= 2)
            return arc(target, values, draw);
        if (!drawTo(target, draw))
            return false;
        position = target;
        return true;
    }

  public:
    explicit Gcode(const QByteArray &bytes) : input(bytes) {}
    PlotImport::Result run() {
        if (!lex())
            return {{}, error};
        for (const auto &item : blocks)
            if (!block(item))
                return {{}, error};
        if (!flush())
            return {{}, error};
        if (paths.isEmpty())
            return {{},
                    "Keine zeichnenden GCode-Pfade. Bei M/S-Wörtern sind M3/M4 und S > 0 "
                    "erforderlich."};
        QRectF bounds = paths.first().boundingRect();
        for (const auto &path : paths) {
            const auto r = path.boundingRect();
            bounds =
                QRectF(QPointF(std::min(bounds.left(), r.left()), std::min(bounds.top(), r.top())),
                       QPointF(std::max(bounds.right(), r.right()),
                               std::max(bounds.bottom(), r.bottom())));
        }
        if (bounds.width() > Geometry::coordinateLimit ||
            bounds.height() > Geometry::coordinateLimit)
            return {{}, "GCode-Ausdehnung größer als 2000 mm; kein Teilimport."};
        QTransform place;
        place.translate(-bounds.left(), -bounds.top());
        for (auto &path : paths)
            path = place.map(path);
        return {
            paths, {}, spindleAware ? "Laser-/Spindelpfade" : "Vorschubpfade ohne M/S-Steuerung"};
    }
};
} // namespace

PlotImport::Result PlotImport::gcode(const QByteArray &bytes) {
    if (bytes.size() > 16 * 1024 * 1024)
        return {{}, "GCode-Datei größer als 16 MiB."};
    return Gcode(bytes).run();
}
