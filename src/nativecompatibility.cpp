#include "nativecompatibility.h"
#include <QCryptographicHash>
#include <QHash>
#include <QSet>
#include <QXmlStreamReader>
#include <algorithm>
namespace NativeDesign {
namespace {
constexpr qsizetype maximumBytes = 16 * 1024 * 1024;
class Scan {
  public:
    CompatibilityReport report;
    QHash<QString, int> aggregation;
    void issue(QString code, QString label, QString scope = {}, QString field = {}, int index = -1,
               bool block = false, QString severity = "warning") {
        report.blocked = report.blocked || block;
        report.canImportGeometry = report.canImportGeometry && !block;
        const auto key = code + "\n" + scope + "\n" + field;
        if (aggregation.contains(key)) {
            ++report.issues[aggregation[key]].count;
            return;
        }
        if (report.issues.size() >= 127) {
            report.complete = false;
            report.blocked = true;
            report.canImportGeometry = false;
            if (report.issues.size() == 127)
                report.issues.append({"report-limit",
                                      "error",
                                      "document",
                                      {},
                                      "Kompatibilitätsprüfung überschreitet Berichtsgrenze.",
                                      -1,
                                      1});
            return;
        }
        aggregation[key] = report.issues.size();
        report.issues.append({std::move(code), std::move(severity), std::move(scope),
                              std::move(field), std::move(label), index, 1});
    }
};
QStringList shapeAttributes(const QString &type) {
    QStringList names{"Type", "LayerIndex", "CutIndex", "OpIndex"};
    if (type == "Path")
        names << "VertID" << "PrimID";
    else if (type == "Rect")
        names << "W" << "H" << "Cr";
    else if (type == "Ellipse")
        names << "Rx" << "Ry";
    else if (type == "Text")
        names << "Str" << "Font" << "H";
    else if (type == "Bitmap")
        names << "W" << "H" << "Gamma" << "Contrast" << "Brightness" << "EnhanceAmount"
              << "EnhanceRadius" << "EnhanceDenoise" << "File" << "SourceHash" << "Data";
    return names;
}
} // namespace
QVariantMap CompatibilityReport::toVariantMap() const {
    QVariantList rows;
    for (const auto &issue : issues)
        rows.append(QVariantMap{{"code", issue.code},
                                {"severity", issue.severity},
                                {"scope", issue.scope},
                                {"field", issue.field},
                                {"label", issue.label},
                                {"sourceIndex", issue.sourceIndex},
                                {"count", issue.count}});
    return {{"format", format},
            {"sourceSha256", QString::fromLatin1(sourceSha256)},
            {"issues", rows},
            {"complete", complete},
            {"geometryOnlyRequired", geometryOnlyRequired},
            {"blocked", blocked},
            {"canImportGeometry", canImportGeometry},
            {"geometryValidated", geometryValidated},
            {"semanticMigrationSupported", false}};
}
CompatibilityReport compatibility(const QByteArray &bytes, bool millmage, ReadOptions options) {
    Scan scan;
    scan.report.format = millmage ? "MillMage" : "LightBurn";
    if (bytes.size() > maximumBytes) {
        scan.issue("input-limit", "Native Datei überschreitet 16 MiB.", "document", {}, -1, true,
                   "error");
        scan.report.complete = false;
        return scan.report;
    }
    scan.report.sourceSha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    scan.issue("geometry-only-import",
               "Nur Grafikgeometrie wird übernommen; Zielmaterial und Zieloperationen müssen "
               "ausdrücklich neu zugeordnet werden.",
               "document");

    QXmlStreamReader xml(bytes);
    int depth = 0, nodes = 0, shapeIndex = -1;
    QStringList ancestors;
    QSet<int> backupTextShapes;
    QSet<int> textShapes;
    // Keep text ancestry explicit; nested BackupPath geometry does not imply a
    // backup for a different text object.
    QList<QPair<int, int>> textStack;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isDTD() || xml.isEntityReference() || xml.isProcessingInstruction()) {
            scan.issue("external-resource-rejected",
                       "DTD, XML-Verarbeitungsanweisungen und Entitätsverweise sind nicht erlaubt.",
                       "document", {}, -1, true, "error");
            scan.report.complete = false;
            break;
        }
        if (xml.isStartElement()) {
            if (++depth > 64 || ++nodes > 100000) {
                scan.issue("xml-work-limit", "XML-Arbeitsgrenze erreicht.", "document", {}, -1,
                           true, "error");
                scan.report.complete = false;
                break;
            }
            const auto tag = xml.name().toString();
            const auto attrs = xml.attributes();
            if (depth == 1) {
                if (tag != (millmage ? "MillMageProject" : "LightBurnProject"))
                    scan.issue("format-mismatch",
                               "Projektkennung passt nicht zum gewählten Format.", "document", {},
                               -1, true, "error");
                if (attrs.value("FormatVersion").toString() != "1" &&
                    (millmage || attrs.hasAttribute("FormatVersion")))
                    scan.issue("unsupported-version",
                               "Native Formatversion wird nicht unterstützt.", "project",
                               "FormatVersion", -1, true, "error");
                const QStringList knownRoot{
                    "AppVersion", "FormatVersion", "MaterialHeight", "MirrorX",   "MirrorY",
                    "Origin",     "OriginX",       "OriginY",        "StartFrom", "JobOrigin",
                    "Units",      "Unit",          "DistanceUnits"};
                for (const auto &attribute : attrs)
                    if (!knownRoot.contains(attribute.name().toString()))
                        scan.issue("unsupported-project-option",
                                   "Unbekannte Projektoption könnte die Geometrie verändern und "
                                   "wird abgewiesen.",
                                   "project", "unrecognized-option", -1, true, "error");
                for (const QString key : {"MirrorX", "MirrorY"})
                    if (attrs.value(key).toString().compare("true", Qt::CaseInsensitive) == 0)
                        scan.issue("unverified-mirror",
                                   "Native Projektspiegelung ist nicht verifiziert; "
                                   "Geometrieimport wird abgewiesen.",
                                   "project", key, -1, true, "error");
                for (const QString key : {"MirrorX", "MirrorY", "Origin", "OriginX", "OriginY",
                                          "StartFrom", "JobOrigin"})
                    if (attrs.hasAttribute(key))
                        scan.issue("source-origin-not-migrated",
                                   "Quellursprung, Startmodus und Spiegelungsoptionen werden nicht "
                                   "als Geräteposition übernommen.",
                                   "project", key);
                for (const QString key : {"Units", "Unit", "DistanceUnits"})
                    if (attrs.hasAttribute(key))
                        scan.issue("coordinate-units-unverified",
                                   "Explizite native Koordinateneinheit ist nicht durch diesen "
                                   "Leser verifiziert.",
                                   "project", key, -1, true, "error");
            }
            if (depth == 2 &&
                !QStringList{"Shape", "Layer", "Thumbnail", "CNCProjectSetup", "VariableText",
                             "UIPrefs", "Operation", "Notes", "CutSetting", "CutSetting_Img"}
                     .contains(tag))
                scan.issue("unsupported-project-section",
                           "Unbekannter Projektabschnitt wird nicht stillschweigend verworfen.",
                           "project", "unrecognized-section", -1, true, "error");
            if (tag == "CutSetting" || tag == "CutSetting_Img")
                scan.issue("laser-operations-not-migrated",
                           "Quell-Laseroperation einschließlich Modus, Leistungswerten, "
                           "Geschwindigkeits-/Einheitenvertrag, Durchgängen und Unteroperationen "
                           "wird nicht übernommen.",
                           "operation", tag);
            if (tag == "Operation" || tag == "CNCProjectSetup")
                scan.issue("cnc-operations-not-migrated",
                           "Fräsoperationen, Werkzeug-, Zustell-, Vorschub-/Einheiten- und "
                           "Spindelparameter werden nicht übernommen.",
                           "operation", tag);
            const bool operationContext =
                ancestors.contains("CutSetting") || ancestors.contains("CutSetting_Img") ||
                ancestors.contains("Operation") || ancestors.contains("CNCProjectSetup");
            const auto lowerTag = tag.toLower();
            if (operationContext) {
                if (QStringList{"power", "maxpower", "minpower", "spindlerpm", "rpm"}.contains(
                        lowerTag))
                    scan.issue("power-not-migrated",
                               "Leistungs- oder Spindelsollwert wird nicht übernommen.",
                               "operation", "power");
                if (QStringList{"speed", "feed", "feedrate", "units", "speedunits", "feedunits",
                                "plungerate"}
                        .contains(lowerTag))
                    scan.issue("feed-units-not-migrated",
                               "Quellvorschub und dessen Einheiten werden nicht übernommen oder "
                               "umgerechnet.",
                               "operation", "feed-or-units");
                if (QStringList{"numpasses", "passes", "suboperations", "suboperation", "sublayers",
                                "sublayer"}
                        .contains(lowerTag))
                    scan.issue("passes-suboperations-not-migrated",
                               "Quelldurchgänge und Unteroperationen werden nicht übernommen.",
                               "operation", "repeats-or-suboperations");
                if (QStringList{"output", "dooutput", "enabled"}.contains(lowerTag) &&
                    (attrs.value("Value") == "0" ||
                     attrs.value("Value").toString().compare("false", Qt::CaseInsensitive) == 0))
                    scan.issue("source-disabled-output-not-preserved",
                               "Deaktivierte Quell-Ausgabe bleibt keine Sperre für neu zugeordnete "
                               "Zieloperationen; Ausgabe ausdrücklich prüfen.",
                               "operation", "output");
                if (QStringList{"airassist", "air", "output", "dooutput", "enabled"}.contains(
                        lowerTag))
                    scan.issue("auxiliary-output-not-migrated",
                               "Quell-Ausgabe- und Air-Assist-Einstellungen werden nicht als "
                               "Zubehörfreigabe übernommen.",
                               "operation", "auxiliary-output");
                if (QStringList{"kerf", "zoffset", "zstep", "offset"}.contains(lowerTag))
                    scan.issue(
                        "offsets-not-migrated",
                        "Quell-Schnittfugen-, Zustell- oder Achsenversatz wird nicht übernommen.",
                        "operation", "offset");
            }
            if (QStringList{"XForm", "VertList", "PrimList", "BackupPath", "Children"}.contains(
                    tag) &&
                !attrs.isEmpty())
                scan.issue("unsupported-geometry-option",
                           "Nicht ausgewertete Option eines Geometriecontainers wird abgewiesen.",
                           "geometry", "unrecognized-option", shapeIndex, true, "error");
            if (tag == "VariableText")
                scan.issue("variable-text-not-migrated",
                           "Variablen-/Serientextlogik wird nicht übernommen.", "project", tag);
            if (tag == "UIPrefs" || tag == "Notes")
                scan.issue(
                    "project-metadata-not-migrated",
                    "Quellnotizen und Oberflächen-/Projektpräferenzen werden nicht übernommen.",
                    "project", tag);
            if (tag.compare("CustomGCode", Qt::CaseInsensitive) == 0 ||
                tag.contains("GCode", Qt::CaseInsensitive))
                scan.issue("source-commands-discarded",
                           "Quellbefehle werden weder ausgeführt noch in die Ausgabe kopiert.",
                           "operation", "GCode");
            if (tag.compare("Script", Qt::CaseInsensitive) == 0 ||
                tag.compare("Include", Qt::CaseInsensitive) == 0 ||
                tag.compare("Import", Qt::CaseInsensitive) == 0)
                scan.issue("external-resource-rejected",
                           "Skript- und externe Importressourcen werden abgewiesen.", "document",
                           "resource-reference", -1, true, "error");
            for (const auto &attribute : attrs) {
                const auto key = attribute.name().toString().toLower();
                if (key == "href" || key == "src" || key == "url" || key == "script" ||
                    key == "scriptfile")
                    scan.issue("external-resource-rejected",
                               "Externe Bild-, Skript- oder Verweisressource wird abgewiesen.",
                               "document", "resource-reference", shapeIndex, true, "error");
            }
            if (tag == "Shape") {
                ++shapeIndex;
                if (!ancestors.isEmpty() && ancestors.last() == "BackupPath" &&
                    !textStack.isEmpty())
                    backupTextShapes.insert(textStack.last().first);
                const auto type = attrs.value("Type").toString();
                if (!QStringList{"Path", "Rect", "Ellipse", "Group", "Text", "Bitmap"}.contains(
                        type))
                    scan.issue("unsupported-shape",
                               "Formtyp wird nicht verlustarm als Geometrie gelesen.", "shape",
                               "Type", shapeIndex, true, "error");
                const auto allowed = shapeAttributes(type);
                for (const auto &attribute : attrs)
                    if (!allowed.contains(attribute.name().toString()))
                        scan.issue("unsupported-shape-option",
                                   "Unbekannte Formoption könnte die Geometrie verändern und wird "
                                   "abgewiesen.",
                                   "shape", "unrecognized-option", shapeIndex, true, "error");
                if (attrs.hasAttribute("CutIndex") || attrs.hasAttribute("OpIndex"))
                    scan.issue("operation-assignment-not-migrated",
                               "Quell-Bearbeitungszuordnung wird verworfen; nur bekannte nicht "
                               "ausgebende Werkzeugebenen bleiben Werkzeugebenen.",
                               "shape", attrs.hasAttribute("CutIndex") ? "CutIndex" : "OpIndex",
                               shapeIndex);
                if (type == "Text") {
                    textShapes.insert(shapeIndex);
                    textStack.append({shapeIndex, depth});
                    scan.issue("text-editability-lost",
                               "Text wird zu Konturen; Textinhalt, Variablen, Ausrichtung und "
                               "Editierbarkeit sind kein verlustfreier Textimport.",
                               "shape", "Text", shapeIndex);
                }
                if (type == "Bitmap") {
                    for (const QString key : {"Gamma", "Contrast", "Brightness", "EnhanceAmount",
                                              "EnhanceRadius", "EnhanceDenoise"})
                        if (attrs.hasAttribute(key)) {
                            bool valid = false;
                            const double value = attrs.value(key).toDouble(&valid);
                            if (!valid || value != (key == "Gamma" ? 1.0 : 0.0))
                                scan.issue("unsupported-bitmap-adjustment",
                                           "Quell-Bildanpassung kann die Rastergeometrie oder "
                                           "Tonwerte verändern und wird abgewiesen.",
                                           "shape", key, shapeIndex, true, "error");
                        }
                    scan.issue("bitmap-processing-not-migrated",
                               "Eingebettetes PNG wird geometrisch übernommen; Quellrasterung, "
                               "Laserbildmodus und Bearbeitungsparameter werden nicht übernommen.",
                               "shape", "Bitmap", shapeIndex);
                    if (!attrs.value("File").isEmpty())
                        scan.issue("external-reference-ignored",
                                   "Bild-Dateipfad ist nur verworfene Quellreferenz; "
                                   "ausschließlich eingebettetes PNG wird verwendet.",
                                   "shape", "File", shapeIndex);
                    if (attrs.value("Data").isEmpty())
                        scan.issue("external-resource-rejected",
                                   "Bild ohne eingebettete PNG-Daten wird abgewiesen.", "shape",
                                   "Data", shapeIndex, true, "error");
                }
            }
            ancestors.append(tag);
        } else if (xml.isEndElement()) {
            if (!textStack.isEmpty() && textStack.last().second == depth)
                textStack.removeLast();
            if (!ancestors.isEmpty())
                ancestors.removeLast();
            --depth;
        }
    }
    if (xml.hasError()) {
        scan.issue("invalid-xml", "Ungültiges natives Projekt-XML.", "document", {}, -1, true,
                   "error");
        scan.report.complete = false;
    }
    auto textIndices = textShapes.values();
    std::sort(textIndices.begin(), textIndices.end());
    for (const int index : textIndices)
        if (!backupTextShapes.contains(index))
            scan.issue(
                "font-substitution-required",
                "Text ohne gespeicherte Konturen benötigt ausdrücklich erlaubte "
                "Systemschrift-Ersetzung; Schriftgewicht, Abstände und Geometrie können abweichen.",
                "shape", "Font", index, millmage || !options.allowSystemFontSubstitution,
                millmage || !options.allowSystemFontSubstitution ? "error" : "warning");
    if (options.intent != ImportIntent::GeometryOnly) {
        scan.issue("semantic-migration-unavailable",
                   "Projektmigration mit Erhalt der Bearbeitungssemantik wird nicht unterstützt. "
                   "Grafikimport ausdrücklich auswählen.",
                   "document", {}, -1, false, "error");
        scan.report.blocked = true;
    }
    return scan.report;
}
} // namespace NativeDesign
