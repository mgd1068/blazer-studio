#include "document.h"
#include "geometry.h"
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QXmlStreamReader>
#include <cmath>
#include <functional>
namespace {
QString uid() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
} // namespace
bool Document::importFile(const QUrl &url) {
    if (!url.isLocalFile())
        return false;
    auto ext = QFileInfo(url.toLocalFile()).suffix().toLower();
    if (ext == "stl" || ext == "obj")
        return importModel(url);
    if (ext == "svg")
        return importSvg(url);
    if (QStringList{"png", "jpg", "jpeg", "bmp", "gif", "tif", "tiff", "webp"}.contains(ext))
        return importImage(url);
    if (ext == "blazer")
        return importProject(url);
    if (ext == "pdf" || ext == "ai" || ext == "eps") {
        auto executable = QStandardPaths::findExecutable("inkscape");
        if (executable.isEmpty()) {
            notify("PDF/AI benötigt das lokal installierte Inkscape.");
            return false;
        }
        QTemporaryDir temp;
        auto path = temp.filePath("converted.svg");
        QProcess process;
        process.start(executable, {url.toLocalFile(), "--export-type=svg", "--export-plain-svg",
                                   "--export-text-to-path", "--export-filename=" + path});
        if (!process.waitForStarted(3000) || !process.waitForFinished(30000) ||
            process.exitCode() != 0) {
            process.kill();
            notify("PDF/AI-Konvertierung fehlgeschlagen. Nur von Inkscape unterstützte "
                   "PDF-/AI-Dateien können importiert werden.");
            return false;
        }
        bool ok = importSvg(QUrl::fromLocalFile(path));
        if (ok)
            notify("Erste PDF/AI-Seite als SVG importiert; Pfadvorschau prüfen.");
        return ok;
    }
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024)
        return false;
    QList<Artwork> imported;
    if (ext == "dxf") {
        auto lines = QString::fromUtf8(file.readAll()).split('\n');
        struct Pair {
            int code;
            QString value;
        };
        QList<Pair> pairs;
        for (int i = 0; i + 1 < lines.size(); i += 2) {
            bool ok;
            int code = lines[i].trimmed().toInt(&ok);
            if (!ok) {
                notify("Ungültiges ASCII-DXF.");
                return false;
            }
            pairs.append({code, lines[i + 1].trimmed()});
        }
        double unit = 1;
        for (int i = 0; i + 1 < pairs.size(); ++i)
            if (pairs[i].value == "$INSUNITS") {
                int u = pairs[i + 1].value.toInt();
                if (u == 1)
                    unit = 25.4;
                else if (u == 5)
                    unit = 10;
                else if (u == 6)
                    unit = 1000;
                else if (u != 0 && u != 4) {
                    notify("DXF-Einheit nicht unterstützt.");
                    return false;
                }
            }
        bool entities = false;
        QStringList unsupported;
        for (int i = 0; i < pairs.size();) {
            if (pairs[i].value == "ENTITIES") {
                entities = true;
                ++i;
                continue;
            }
            if (!entities) {
                ++i;
                continue;
            }
            if (pairs[i].code != 0) {
                ++i;
                continue;
            }
            QString type = pairs[i++].value;
            if (type == "ENDSEC")
                break;
            QList<Pair> data;
            while (i < pairs.size() && pairs[i].code != 0)
                data.append(pairs[i++]);
            auto value = [&](int code, double fallback = 0.) {
                for (auto p : data)
                    if (p.code == code) {
                        bool ok;
                        double n = p.value.toDouble(&ok);
                        return ok && std::isfinite(n) ? n
                                                      : std::numeric_limits<double>::quiet_NaN();
                    }
                return fallback;
            };
            QPainterPath path;
            if (type == "LINE") {
                path.moveTo(value(10) * unit, value(20) * unit);
                path.lineTo(value(11) * unit, value(21) * unit);
            } else if (type == "CIRCLE" || type == "ARC") {
                double r = value(40) * unit;
                if (r <= 0 || !std::isfinite(r))
                    return false;
                QPainterPath circle;
                double start = type == "ARC" ? value(50) : 0, end = type == "ARC" ? value(51) : 360;
                if (end <= start)
                    end += 360;
                circle.arcMoveTo(QRectF(-r, -r, 2 * r, 2 * r), start);
                circle.arcTo(QRectF(-r, -r, 2 * r, 2 * r), start, end - start);
                QTransform t;
                t.translate(value(10) * unit, value(20) * unit);
                t.scale(1, -1);
                path = t.map(circle);
            } else if (type == "LWPOLYLINE") {
                bool first = true;
                double x = 0;
                for (auto p : data) {
                    if (p.code == 42 && p.value.toDouble() != 0) {
                        unsupported.append("LWPOLYLINE mit Bulge");
                        path = {};
                        break;
                    }
                    if (p.code == 10)
                        x = p.value.toDouble() * unit;
                    if (p.code == 20) {
                        auto y = p.value.toDouble() * unit;
                        if (first)
                            path.moveTo(x, y);
                        else
                            path.lineTo(x, y);
                        first = false;
                    }
                }
                if (int(value(70)) & 1)
                    path.closeSubpath();
            } else if (type == "POINT")
                continue;
            else {
                unsupported.append(type);
                continue;
            }
            if (path.isEmpty())
                continue;
            Artwork a;
            a.id = uid();
            a.name = type;
            a.layer = 1;
            a.page = m_activePage;
            if (!bakePath(a, path)) {
                notify("Ungültige DXF-Geometrie.");
                return false;
            }
            imported.append(a);
        }
        if (!unsupported.isEmpty()) {
            unsupported.removeDuplicates();
            notify("DXF enthält nicht unterstützte Elemente: " + unsupported.join(", ") +
                   ". Import vollständig abgebrochen.");
            return false;
        }
    } else if (ext == "lbrn2" || ext == "lbrn") {
        auto bytes = file.readAll();
        QXmlStreamReader check(bytes);
        while (!check.atEnd()) {
            check.readNext();
            if (check.isDTD()) {
                notify("DTD in LightBurn-Projekt abgelehnt.");
                return false;
            }
        }
        if (check.hasError())
            return false;
        QDomDocument xml;
        if (!xml.setContent(bytes))
            return false;
        auto root = xml.documentElement();
        if (root.tagName() != "LightBurnProject")
            return false;
        QString error;
        int shapes = 0;
        auto transform = [](const QString &text, QTransform &t) {
            auto values = text.simplified().split(' ');
            if (values.size() != 6)
                return false;
            double v[6];
            for (int i = 0; i < 6; ++i) {
                bool ok;
                v[i] = values[i].toDouble(&ok);
                if (!ok || !std::isfinite(v[i]) || std::abs(v[i]) > 1e6)
                    return false;
            }
            t = QTransform(v[0], v[1], v[2], v[3], v[4], v[5]);
            return true;
        };
        auto pathData = [&](QDomElement element, QPainterPath &path) {
            QString text = element.firstChildElement("VertList").text(),
                    primitives = element.firstChildElement("PrimList").text();
            struct Vertex {
                QPointF point, left, right;
            };
            QList<Vertex> vertices;
            const QString n = "([+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?)";
            QRegularExpression point("^" + n + " +" + n), control("c([01])([xy])" + n);
            for (auto chunk : text.split('V', Qt::SkipEmptyParts)) {
                auto match = point.match(chunk);
                if (!match.hasMatch())
                    return false;
                Vertex vertex;
                vertex.point = {match.captured(1).toDouble(), match.captured(2).toDouble()};
                vertex.left = vertex.right = vertex.point;
                QMap<QString, double> handles;
                auto cs = control.globalMatch(chunk);
                while (cs.hasNext()) {
                    auto c = cs.next();
                    handles[c.captured(1) + c.captured(2)] = c.captured(3).toDouble();
                }
                if (handles.contains("0x") && handles.contains("0y"))
                    vertex.left = QPointF(handles["0x"], handles["0y"]);
                if (handles.contains("1x") && handles.contains("1y"))
                    vertex.right = QPointF(handles["1x"], handles["1y"]);
                vertices.append(vertex);
                if (vertices.size() > 100000)
                    return false;
            }
            QRegularExpression primitive("([LB])([0-9]+) +([0-9]+)");
            auto matches = primitive.globalMatch(primitives);
            bool any = false;
            int previous = -1;
            while (matches.hasNext()) {
                auto match = matches.next();
                int a = match.captured(2).toInt(), b = match.captured(3).toInt();
                if (a < 0 || b < 0 || a >= vertices.size() || b >= vertices.size())
                    return false;
                auto from = vertices[a], to = vertices[b];
                if (a != previous)
                    path.moveTo(from.point);
                if (match.captured(1) == "L")
                    path.lineTo(to.point);
                else
                    path.cubicTo(from.right, to.left, to.point);
                previous = b;
                any = true;
            }
            return any;
        };
        std::function<bool(QDomElement, QTransform, QString)> readShape;
        readShape = [&](QDomElement element, QTransform parent, QString group) {
            if (++shapes > Geometry::objectLimit)
                return false;
            QString type = element.attribute("Type");
            QTransform local;
            auto xf = element.firstChildElement("XForm");
            if (!xf.isNull() && !transform(xf.text(), local))
                return false;
            QTransform combined = local * parent;
            if (type == "Group") {
                QString id = group.isEmpty() ? uid() : group;
                auto children = element.firstChildElement("Children");
                for (auto child = children.firstChildElement("Shape"); !child.isNull();
                     child = child.nextSiblingElement("Shape"))
                    if (!readShape(child, combined, id))
                        return false;
                return true;
            }
            QPainterPath path;
            if (type == "Rect") {
                double w = element.attribute("W").toDouble(), h = element.attribute("H").toDouble(),
                       radius = element.attribute("Cr").toDouble();
                path.addRoundedRect(QRectF(-w / 2, -h / 2, w, h), radius, radius);
            } else if (type == "Ellipse") {
                double rx = element.attribute("Rx").toDouble(),
                       ry = element.attribute("Ry").toDouble();
                path.addEllipse(QRectF(-rx, -ry, rx * 2, ry * 2));
            } else if (type == "Path") {
                if (!pathData(element, path)) {
                    error = "Ungültiger LightBurn-Pfad";
                    return false;
                }
            } else if (type == "Text") {
                auto backup = element.firstChildElement("BackupPath").firstChildElement("Shape");
                if (!backup.isNull()) {
                    return readShape(backup, combined, group);
                }
                QFont font(element.attribute("Font").section(',', 0, 0));
                font.setPixelSize(100);
                path.addText(0, 0, font, element.attribute("Str"));
                double h = element.attribute("H").toDouble();
                auto b = path.boundingRect();
                if (b.isEmpty() || h <= 0)
                    return false;
                QTransform size;
                size.scale(h / b.height(), h / b.height());
                path = size.map(path);
            } else {
                error = "Nicht unterstützte LightBurn-Form: " + type;
                return false;
            }
            Artwork a;
            a.id = uid();
            a.name = "LightBurn " + QString::number(shapes);
            a.layer = 1;
            a.page = m_activePage;
            a.groupId = group;
            a.groupName = group.isEmpty() ? QString() : "Importgruppe";
            if (!bakePath(a, combined.map(path)))
                return false;
            imported.append(a);
            return true;
        };
        for (auto shape = root.firstChildElement("Shape"); !shape.isNull();
             shape = shape.nextSiblingElement("Shape"))
            if (!readShape(shape, {}, {})) {
                notify(error.isEmpty() ? "LightBurn-Geometrie ungültig; kein Teilimport."
                                       : error + "; kein Teilimport.");
                return false;
            }
        // Laser-specific settings and coordinate origins are deliberately not guessed.
        // Geometry is editable before any user-triggered job. Report migration limitations.
        notify("LightBurn-Geometrie importiert. Schnittparameter und Ursprung/Spiegelung werden "
               "nicht übernommen; vor Ausgabe prüfen.");
    } else {
        notify("Dateiformat nicht unterstützt.");
        return false;
    }
    if (imported.isEmpty() || m_items.size() + imported.size() > Geometry::objectLimit) {
        notify("Keine importierbare Geometrie oder zu viele Objekte.");
        return false;
    }
    checkpoint();
    beginResetModel();
    m_items.append(imported);
    normalizeGroups();
    m_selected = m_items.size() - imported.size();
    m_selection = {m_selected};
    endResetModel();
    refresh();
    notify(QString::number(imported.size()) +
           " Vektorobjekte importiert; Einheiten und Bearbeitungsparameter prüfen.");
    return true;
}
bool Document::importProject(const QUrl &url) {
    if (!url.isLocalFile())
        return false;
    QFile f(url.toLocalFile());
    if (!f.open(QIODevice::ReadOnly))
        return false;
    auto state = parse(f.read(16 * 1024 * 1024 + 1));
    if (!state || m_items.size() + state->items.size() > Geometry::objectLimit ||
        m_layers.size() + state->layers.size() > 64)
        return false;
    auto items = state->items;
    int offset = m_layers.size();
    QMap<QString, QString> groups;
    for (auto &a : items) {
        a.id = uid();
        a.page = m_activePage;
        a.layer += offset;
        for (auto &op : a.extraOperations)
            op += offset;
        if (!a.groupId.isEmpty()) {
            if (!groups.contains(a.groupId))
                groups[a.groupId] = uid();
            a.groupId = groups[a.groupId];
        }
    }
    // Validate the combined project through its normal serializer before committing.
    auto previous = snapshot();
    m_items.append(items);
    m_layers.append(state->layers);
    auto bytes = serialize();
    applySnapshot(previous);
    auto combined = parse(bytes);
    if (!combined) {
        notify("Kombiniertes Projekt überschreitet Grenzen.");
        return false;
    }
    checkpoint();
    applySnapshot(*combined);
    notify("Projekt mit eigenen Operationen eingefügt.");
    return true;
}
bool Document::batch(const QUrl &csv, const QUrl &directory, const QString &pattern) {
    if (!csv.isLocalFile() || !directory.isLocalFile() || pattern.size() > 1000)
        return false;
    QFile f(csv.toLocalFile());
    if (!f.open(QIODevice::ReadOnly) || f.size() > 1024 * 1024)
        return false;
    // RFC-style quoted fields, embedded newlines, escaped quotes.
    auto text = QString::fromUtf8(f.readAll());
    QChar delimiter = text.section('\n', 0, 0).contains(';') ? ';' : ',';
    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    for (int i = 0; i < text.size(); ++i) {
        QChar c = text[i];
        if (c == '"') {
            if (quoted && i + 1 < text.size() && text[i + 1] == '"') {
                field += '"';
                ++i;
            } else
                quoted = !quoted;
        } else if (!quoted && (c == delimiter || c == '\n')) {
            row << field;
            field.clear();
            if (c == '\n') {
                rows << row;
                row.clear();
            }
        } else if (c != '\r')
            field += c;
    }
    if (quoted)
        return false;
    if (!field.isEmpty() || !row.isEmpty()) {
        row << field;
        rows << row;
    }
    if (rows.size() < 2 || rows.size() > 1001)
        return false;
    auto headers = rows.takeFirst();
    if (headers.isEmpty())
        return false;
    QDir dir(directory.toLocalFile());
    if (!dir.exists())
        return false;
    auto original = snapshot();
    QList<QPair<QString, QByteArray>> jobs;
    qint64 stagedBytes = 0;
    for (int r = 0; r < rows.size(); ++r) {
        m_items = original.items;
        for (auto &a : m_items)
            if (a.page == m_activePage && a.kind == "text") {
                QString value = pattern.isEmpty() ? a.text : pattern;
                for (int c = 0; c < headers.size(); ++c)
                    value.replace("{" + headers[c] + "}", rows[r].value(c));
                value.replace("{number}", QString::number(r + 1));
                if (value.size() > 1000) {
                    applySnapshot(original);
                    return false;
                }
                a.text = value;
            }
        m_job.reset();
        auto j = job();
        if (!j.valid()) {
            applySnapshot(original);
            notify("CSV-Zeile " + QString::number(r + 1) + ": " + j.errors.join(";"));
            return false;
        }
        QString name = dir.filePath(QString("job-%1.gcode").arg(r + 1, 4, 10, QChar('0')));
        if (QFile::exists(name)) {
            applySnapshot(original);
            notify("Stapelexport überschreibt keine vorhandenen Jobs.");
            return false;
        }
        auto bytes = (j.gcode().join('\n') + '\n').toUtf8();
        stagedBytes += bytes.size();
        if (stagedBytes > 64 * 1024 * 1024) {
            applySnapshot(original);
            notify("CSV-Stapel überschreitet 64 MiB; kleinere Teilstapel verwenden.");
            return false;
        }
        jobs.append({name, bytes});
    }
    applySnapshot(original);
    QStringList written;
    for (const auto &entry : jobs) {
        QSaveFile out(entry.first);
        if (!out.open(QIODevice::WriteOnly) || out.write(entry.second) != entry.second.size() ||
            !out.commit()) {
            for (auto path : written)
                QFile::remove(path);
            return false;
        }
        written << entry.first;
    }
    notify(QString::number(jobs.size()) + " Jobs exportiert; keine automatische Ausführung.");
    return true;
}
