#include "document.h"
#include "geometry.h"
#include "imagelimits.h"
#include "nativedesign.h"
#include "plotimport.h"
#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <cmath>
#include <functional>
#include <algorithm>
namespace {
QString uid() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
} // namespace
bool Document::importFile(const QUrl &url) {
    return importFileInternal(url);
}
QVariantMap Document::previewNativeImport(const QUrl &url, bool allowSystemFontSubstitution) {
    const auto invalid = [](QString message) {
        return QVariantMap{{"complete",false},{"blocked",true},{"canImportGeometry",false},
                           {"geometryValidated",false},{"sourceSha256",QString()},
                           {"issues",QVariantList{}},{"error",std::move(message)}};
    };
    if (!url.isLocalFile()) return invalid("Eine lokale LightBurn- oder MillMage-Datei auswählen.");
    const QFileInfo info(url.toLocalFile());
    const auto ext = info.suffix().toLower();
    if (!info.isFile() || !QStringList{"lbrn","lbrn2","mage"}.contains(ext))
        return invalid("Kein unterstütztes reguläres natives Projekt.");
    QFile file(info.filePath());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16*1024*1024)
        return invalid("Natives Projekt nicht lesbar oder größer als 16 MiB.");
    const auto bytes = file.read(16*1024*1024+1);
    if (bytes.size() > 16*1024*1024 || !file.atEnd() || file.error() != QFileDevice::NoError)
        return invalid("Natives Projekt konnte nicht vollständig innerhalb des Datenlimits gelesen werden.");
    const auto parsed = NativeDesign::read(bytes,ext=="mage",
                                         {NativeDesign::ImportIntent::GeometryOnly,allowSystemFontSubstitution});
    auto report = parsed.compatibility.toVariantMap();
    report["error"] = parsed.error;
    report["geometryValidated"] = parsed.error.isEmpty() && parsed.compatibility.geometryValidated;
    return report;
}
bool Document::importNativeGeometry(const QUrl &url, const QString &expectedSourceSha256,
                                     bool allowSystemFontSubstitution) {
    if (!url.isLocalFile() || !QFileInfo(url.toLocalFile()).isFile() ||
        !QStringList{"lbrn","lbrn2","mage"}.contains(QFileInfo(url.toLocalFile()).suffix().toLower())) {
        notify("Eine lokale native Projektdatei auswählen; kein Import."); return false;
    }
    if (expectedSourceSha256.size()!=64 || !std::all_of(expectedSourceSha256.cbegin(),expectedSourceSha256.cend(),[](QChar c) {
            return (c>='0' && c<='9') || (c>='a' && c<='f'); })) {
        notify("Zuerst eine unveränderte Importvorschau prüfen; kein Import."); return false;
    }
    return importFileInternal(url,expectedSourceSha256,true,allowSystemFontSubstitution);
}
bool Document::importFileInternal(const QUrl &url, const QString &expectedNativeSha256,
                                 bool nativeGeometryOnly, bool allowSystemFontSubstitution) {
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
    QList<int> pendingNativeGeometry;
    const bool hpgl = ext == "hpgl" || ext == "plt";
    const bool gcode = ext == "gc" || ext == "gcode" || ext == "nc";
    const bool ruida = ext == "rd";
    const bool native = ext == "mage" || ext == "lbrn" || ext == "lbrn2";
    QString plotInterpretation;
    if (hpgl || gcode || ruida) {
        const auto bytes = file.read(16 * 1024 * 1024 + 1);
        const auto result = hpgl ? PlotImport::hpgl(bytes) : ruida ? PlotImport::ruida(bytes) : PlotImport::gcode(bytes);
        if (!result.error.isEmpty()) {
            notify(result.error);
            return false;
        }
        auto paths = result.paths;
        if (gcode || ruida) {
            // A raster job can contain thousands of disconnected strokes.
            // Keep them as subpaths of one editable artwork, without bridges.
            QPainterPath compound;
            for (const auto &path : paths)
                compound.addPath(path);
            paths = {compound};
        }
        for (const auto &path : paths) {
            Artwork a;
            a.id = uid();
            a.name = (hpgl ? "HPGL " : ruida ? "RDWorks " : "GCode ") + QString::number(imported.size() + 1);
            a.layer = 1;
            a.page = m_activePage;
            if (!bakePath(a, path)) {
                notify("Importgeometrie ungültig; kein Teilimport.");
                return false;
            }
            imported.append(a);
        }
        plotInterpretation = result.interpretation;
    } else if (ext == "dxf") {
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
    } else if (native) {
        const auto bytes = file.read(16 * 1024 * 1024 + 1);
        if (!file.atEnd() || file.error()!=QFileDevice::NoError || bytes.size()>16*1024*1024) {
            notify("Natives Projekt überschreitet das Datenlimit oder ist nicht vollständig lesbar; kein Import."); return false;
        }
        if (nativeGeometryOnly && QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()) != expectedNativeSha256) {
            notify("Die native Projektdatei hat sich seit der Vorschau geändert. Erneut prüfen; kein Import."); return false;
        }
        const auto result = NativeDesign::read(bytes, ext == "mage",
            {nativeGeometryOnly ? NativeDesign::ImportIntent::GeometryOnly : NativeDesign::ImportIntent::PreserveProjectSemantics,
             allowSystemFontSubstitution});
        if (!result.error.isEmpty()) {
            notify(result.error + "; kein Teilimport.");
            return false;
        }
        QHash<QString,QString> importedGroups;
        for (const auto &element : result.elements) {
            Artwork a;
            a.id = uid();
            a.name = (ext == "mage" ? "MillMage " : "LightBurn ") + QString::number(imported.size() + 1);
            a.page = m_activePage;
            a.visible = element.visible;
            a.toolLayer = element.toolLayer;
            if (element.toolLayer == 0) {
                pendingNativeGeometry.append(imported.size());
                a.toolLayer = 1; // Explicit geometry import does not enable previously disabled source output.
            }
            if (!element.group.isEmpty()) {
                if (!importedGroups.contains(element.group)) importedGroups.insert(element.group,uid());
                a.groupId = importedGroups.value(element.group);
            }
            a.groupName = a.groupId.isEmpty() ? QString() : "Importgruppe";
            if (element.image) {
                a.kind = "image";
                a.bounds = element.bounds;
                a.reliefAsset = element.image;
                a.layer = -1;
                for (int i = 0; i < m_layers.size(); ++i)
                    if (m_layers[i].toMap()["mode"] == "Gravur") { a.layer = i; break; }
                if (a.layer < 0) {
                    notify("Für eingebettete Bilder zuerst eine Gravuroperation anlegen; kein "
                           "Teilimport.");
                    return false;
                }
                a.style = {{"imageMode", "dither"}, {"brightness", 0.}, {"contrast", 1.}, {"gamma", 1.}};
            } else {
                a.layer = 1;
                if (!bakePath(a, element.path)) {
                    notify("Native Vektorgeometrie ungültig; kein Teilimport.");
                    return false;
                }
            }
            imported.append(a);
        }
        auto proposed = snapshot();
        proposed.items.append(imported);
        proposed.selected = m_items.size();
        proposed.selection = {proposed.selected};
        if (!parse(serialize(proposed))) {
            notify("Kombiniertes Projekt ungültig oder über Daten-/Bildgrenzen; kein Teilimport.");
            return false;
        }
    } else {
        notify("Dateiformat nicht unterstützt.");
        return false;
    }
    if (imported.isEmpty() || m_items.size() + imported.size() > Geometry::objectLimit) {
        notify("Keine importierbare Geometrie oder zu viele Objekte.");
        return false;
    }
    checkpoint("Geometrie importieren");
    beginResetModel();
    m_items.append(imported);
    normalizeGroups();
    m_selected = m_items.size() - imported.size();
    m_selection = {m_selected};
    if (nativeGeometryOnly && !pendingNativeGeometry.isEmpty()) {
        m_selection.clear();
        for (const auto row : pendingNativeGeometry) m_selection.append(m_items.size()-imported.size()+row);
        m_selected = m_selection.first();
    }
    endResetModel();
    refresh();
    notify(QString::number(imported.size()) +
           (native ? " native Grafikobjekte als Vorlagen importiert. Bearbeitung rechts ausdrücklich wählen; Quellparameter wurden nicht übernommen."
            : ruida ? " RDWorks-Pfadobjekt importiert (" + plotInterpretation +
                          "). Gemeinsam links unten platziert, Y nach oben; Leistung, Vorschub, "
                          "Quellen- und Geräteparameter nicht übernommen."
            : gcode ? " GCode-Pfadobjekt importiert (" + plotInterpretation +
                        "). Gemeinsam links unten platziert; "
                        "Leistung, Vorschub, Pausen und Schaltbefehle werden nicht übernommen."
            : hpgl ? " HPGL-Pfade importiert (Plottereinheit 0,025 mm; IP/SC berücksichtigt). "
                     "Gemeinsam links unten platziert. "
                     "Stifte als Geometrie übernommen; Bearbeitungsparameter neu prüfen."
                   : " Vektorobjekte importiert; Einheiten und Bearbeitungsparameter prüfen."));
    return true;
}
bool Document::importProject(const QUrl &url) {
    if (!url.isLocalFile())
        return false;
    QFile f(url.toLocalFile());
    if (!f.open(QIODevice::ReadOnly))
        return false;
    auto state = parse(f.read(ImageLimits::projectBytes + 1));
    if (!state || m_items.size() + state->items.size() > Geometry::objectLimit ||
        m_layers.size() + state->layers.size() > 64)
        return false;
    if (state->deviceProfile.outputFingerprint() != m_deviceProfile.outputFingerprint()) {
        notify("Projektimport abgelehnt: abweichendes Geräteprofil. Geometrie bei Bedarf separat "
               "importieren.");
        return false;
    }
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
    auto proposed = snapshot();
    proposed.items.append(items);
    proposed.layers.append(state->layers);
    for (auto guide : state->guides) {
        guide.id = uid();
        guide.page = m_activePage;
        proposed.guides.append(guide);
    }
    auto combined = parse(serialize(proposed));
    if (!combined) {
        notify("Kombiniertes Projekt überschreitet Grenzen.");
        return false;
    }
    checkpoint("Projekt importieren");
    applySnapshot(*combined);
    notify("Projektinhalt mit eigenen Operationen eingefügt; Name und Notizen des geöffneten "
           "Projekts bleiben erhalten.");
    return true;
}
bool Document::batch(const QUrl &csv, const QUrl &directory, const QString &pattern) {
    if (startMode() != AbsoluteCoordinates) {
        notify("CSV-Stapelexport unterstützt noch keine relativen Startmodi; keine absolute Ersatzdatei erzeugt.");
        return false;
    }
    if (m_background) {
        if (!m_batch.start(jobInput(), csv, directory, pattern)) {
            notify("Ein CSV-Stapel wird bereits exportiert.");
            return false;
        }
        notify("CSV-Stapel wird aus dem aktuellen Projektstand im Hintergrund erzeugt.");
        return true;
    }
    const auto result = BatchExport::run(jobInput(), csv, directory, pattern);
    notify(result.error.isEmpty()
               ? QString::number(result.count) + " Jobs exportiert; keine automatische Ausführung."
               : result.error);
    return result.error.isEmpty() && !result.cancelled;
}


bool Document::exportImage(const QUrl &url, bool processed, const QString &expectedId,
                           quint64 expectedRevision) {
    if (m_selection.size()!=1 || m_selected<0 || m_items[m_selected].kind!="image") {
        notify("Für den Bildexport genau ein Rasterbild auswählen.");
        return false;
    }
    const auto &art = m_items[m_selected];
    if (!expectedId.isEmpty() && (art.id != expectedId || m_historyRevision != expectedRevision)) {
        notify("Bild oder Projekt wurde seit Öffnen des Exportdialogs geändert; Export erneut "
               "öffnen.");
        return false;
    }
    QString error;
    if(!m_imageExport.start({art.reliefAsset,art.style,art.bounds.size(),processed},url,error)) {
        notify(error);
        return false;
    }
    notify("Bildexport läuft aus dem angeforderten Bildstand …");
    return true;
}
