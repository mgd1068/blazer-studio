#include "geometry.h"
#include "plotimport.h"
#include <QTransform>
#include <cmath>
#include <optional>

namespace {
constexpr int limit = 100000;
quint8 swapEnds(quint8 b) {
    return (b & 0x7e) | ((b & 1) << 7) | ((b >> 7) & 1);
}
quint8 decode(quint8 b, quint8 magic) {
    return swapEnds(quint8(b - 1) ^ magic);
}
quint64 number(QByteArrayView data) {
    quint64 value = 0;
    for (quint8 b : data)
        value = (value << 7) | b;
    return value;
}
qint64 relative(QByteArrayView data) {
    const auto value = qint64(number(data));
    return value >= 8192 ? value - 16384 : value;
}
class Ruida {
    QByteArray data;
    QPainterPath path;
    std::optional<QPointF> position;
    bool drawing = false, finished = false, stopped = false, eof = false;
    int commands = 0, elements = 0, strokes = 0;
    QString error;
    bool fail(const QString &message) {
        error = message;
        return false;
    }
    bool zeros(QByteArrayView values) {
        for (quint8 value : values)
            if (value != 0)
                return false;
        return true;
    }
    bool move(quint8 op, QByteArrayView values) {
        if (finished || stopped)
            return fail("RD-Bewegung nach Auftragsende.");
        const int mode = op & 3;
        const bool cut = op >= 0xa8;
        const int size = mode == 0 ? 10 : mode == 1 ? 4 : 2;
        if (values.size() != size)
            return fail("Ungültige RD-Bewegungslänge.");
        QPointF target;
        if (mode == 0) {
            target = {double(number(values.first(5))) / 1000.,
                      -double(number(values.last(5))) / 1000.};
        } else {
            if (!position)
                return fail("RD-Relativbewegung ohne bekannte Startposition.");
            target = *position;
            if (mode != 3)
                target.rx() += relative(values.first(2)) / 1000.;
            if (mode == 1)
                target.ry() -= relative(values.last(2)) / 1000.;
            else if (mode == 3)
                target.ry() -= relative(values) / 1000.;
        }
        if (std::abs(target.x()) > Geometry::coordinateLimit ||
            std::abs(target.y()) > Geometry::coordinateLimit)
            return fail("RD-Koordinate außerhalb ±2000 mm.");
        if (cut) {
            if (!position)
                return fail("RD-Schnitt ohne bekannte Startposition.");
            if (target != *position) {
                if (!drawing) {
                    path.moveTo(*position);
                    ++elements;
                    ++strokes;
                }
                if (++elements > limit)
                    return fail("Zu viele RD-Pfadelemente.");
                path.lineTo(target);
                drawing = true;
            }
        } else
            drawing = false;
        position = target;
        return true;
    }
    bool metadata(quint8 op, QByteArrayView values) {
        if (op == 0xd7) {
            if (!values.empty() || !finished)
                return fail("Ungültiges RD-Dateiende.");
            eof = true;
            return true;
        }
        if (op == 0xeb) {
            if (!values.empty() || finished)
                return fail("Ungültiges RD-Auftragsende.");
            finished = true;
            drawing = false;
            return true;
        }
        if (op == 0xf0)
            return (values.empty() && !position && !finished) || fail("RD-Reset im Auftrag.");
        if (values.empty())
            return fail("Unvollständiger RD-Befehl.");
        const quint8 sub = quint8(values[0]);
        const auto args = values.sliced(1);
        int expected = -1;
        if (op == 0xc6) {
            if ((sub >= 1 && sub <= 8 && sub != 3 && sub != 4) || sub == 0x21 || sub == 0x22 ||
                sub == 0x50 || sub == 0x51 || sub == 0x55 || sub == 0x56)
                expected = 2;
            else if ((sub >= 0x31 && sub <= 0x38 && sub != 0x33 && sub != 0x34) || sub == 0x41 ||
                     sub == 0x42)
                expected = 3;
            else if (sub == 0x10 || sub == 0x12 || sub == 0x13 || sub == 0x15 || sub == 0x16)
                expected = 5;
        } else if (op == 0xc9)
            expected = sub == 2 ? 5 : sub == 4 ? 6 : -1;
        else if (op == 0xca) {
            if (sub == 1 || sub == 2 || sub == 3 || sub == 0x10 || sub == 0x22)
                expected = 1;
            else if (sub == 6)
                expected = 6;
            else if (sub == 0x41)
                expected = 2;
        } else if (op == 0xd8 && sub == 0) {
            return ((args.empty() || (args.size() == 1 && args[0] == 0)) && !finished) ||
                   fail("Unbekannter RD-Startmodus.");
        } else if (op == 0xe7) {
            if (sub == 0) {
                if (!args.empty())
                    return fail("Ungültiger RD-Stopp.");
                stopped = true;
                drawing = false;
                return true;
            }
            if (sub == 3 || sub == 7 || sub == 0x13 || sub == 0x17 || sub == 0x23 || sub == 0x50 ||
                sub == 0x51)
                expected = 10; // Bounds only, never a movement.
            else if (sub == 5 || sub == 0x24 || sub == 0x60)
                expected = 1;
            else if (sub == 0x52 || sub == 0x53 || sub == 0x61 || sub == 0x62)
                expected = 11;
            else if (sub == 4 || sub == 8) {
                if (args.size() != 14 || number(args.first(2)) != 1 ||
                    number(args.sliced(2, 2)) != 1)
                    return fail("RD-Matrixwiederholungen noch nicht unterstützt.");
                return true;
            } else if (sub == 6) {
                return (args.size() == 10 && zeros(args)) ||
                       fail("RD-Materialvorschub noch nicht unterstützt.");
            } else if (sub == 0x54 || sub == 0x55) {
                return (args.size() == 6 && zeros(args.sliced(1))) ||
                       fail("RD-Quellenversatz noch nicht unterstützt.");
            }
        } else if (op == 0xf1) {
            if (sub <= 2)
                return (args.size() == 1 && zeros(args) && !position) ||
                       fail("RD-Konfigurationswechsel im Auftrag.");
            if (sub == 3)
                return (args.size() == 10 && zeros(args)) ||
                       fail("RD-Zweitquellenversatz noch nicht unterstützt.");
            if (sub == 4)
                return (args.size() == 1 && zeros(args)) ||
                       fail("Automatischer RD-Materialvorschub noch nicht unterstützt.");
        } else if (op == 0xf2) {
            if (sub <= 1 || sub == 7)
                expected = 1;
            else if ((sub >= 2 && sub <= 4) || sub == 6)
                expected = 10;
            else if (sub == 5) {
                if (args.size() != 14 || number(args.first(2)) != 1 ||
                    number(args.sliced(2, 2)) != 1)
                    return fail("RD-Matrixwiederholungen noch nicht unterstützt.");
                return true;
            }
        } else if (op == 0xea && sub == 0)
            expected = 0;
        else if (op == 0xda && sub == 1 && finished)
            expected = 12; // Recorded work/travel distances, not job geometry.
        return (expected >= 0 && args.size() == expected) ||
               fail(QString("Unbekannter oder unvollständiger RD-Befehl %1/%2.")
                        .arg(op, 2, 16, QLatin1Char('0'))
                        .arg(sub, 2, 16, QLatin1Char('0')));
    }

  public:
    PlotImport::Result run(const QByteArray &bytes) {
        if (bytes.size() > 16 * 1024 * 1024 || bytes.size() < 4)
            return {{}, "RD-Datei leer, unvollständig oder größer als 16 MiB."};
        std::optional<quint8> magic;
        for (quint8 candidate : {0x88, 0x11})
            if (decode(quint8(bytes[0]), candidate) == 0xd8 &&
                decode(quint8(bytes[1]), candidate) == 0x12)
                magic = candidate;
        if (!magic)
            return {
                {},
                "Unbekannte RD-Kodierung; erwartet wird ein RDWorks-Job, keine RLD-Projektdatei."};
        data.reserve(bytes.size());
        for (quint8 b : bytes)
            data.append(char(decode(b, *magic)));
        qsizetype cursor = 2; // Validated D8/12 job-upload header.
        while (cursor < data.size()) {
            if (eof)
                return {{}, "Daten nach RD-Dateiende."};
            const quint8 op = quint8(data[cursor++]);
            if (op < 0x80 || ++commands > limit)
                return {{}, "RD-Befehlsgrenze oder ungültige Befehlskennung."};
            const auto start = cursor;
            while (cursor < data.size() && quint8(data[cursor]) < 0x80)
                ++cursor;
            const QByteArrayView values(data.constData() + start, cursor - start);
            const bool movement = (op >= 0x88 && op <= 0x8b) || (op >= 0xa8 && op <= 0xab);
            if (!(movement ? move(op, values) : metadata(op, values)))
                return {{}, error};
        }
        if (!eof || !finished || elements < 2)
            return {{}, "RD-Datei ohne vollständiges Ende oder ohne Schnittgeometrie."};
        const auto bounds = path.boundingRect();
        if (bounds.width() > Geometry::coordinateLimit ||
            bounds.height() > Geometry::coordinateLimit)
            return {{}, "RD-Ausdehnung größer als 2000 mm."};
        QTransform placement;
        placement.translate(-bounds.left(), -bounds.top());
        return {{placement.map(path)}, {}, QString::number(strokes) + " getrennte Schnittwege"};
    }
};
} // namespace
PlotImport::Result PlotImport::ruida(const QByteArray &bytes) {
    return Ruida().run(bytes);
}
