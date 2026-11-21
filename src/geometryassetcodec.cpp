#include "geometryassetcodec.h"
#include "geometry.h"
#include "imageprocessing.h"
#include "job.h"
#include "vectorutil.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSet>
#include <QUuid>
#include <cmath>
namespace GeometryAssetCodec {
namespace {
constexpr int maximumElements = 100000, maximumItems = 2000;
constexpr qint64 maximumPixels = 32 * 1024 * 1024;
QString uuid() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
bool keys(const QJsonObject &value, const QStringList &expected) {
    if (value.size() != expected.size())
        return false;
    for (const auto &key : expected)
        if (!value.contains(key))
            return false;
    return true;
}
bool number(const QJsonValue &value, double low, double high) {
    return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() >= low &&
           value.toDouble() <= high;
}
QJsonArray rect(QRectF r) {
    return {r.x(), r.y(), r.width(), r.height()};
}
bool rectangle(const QJsonValue &value, QRectF &out, double width, double height) {
    if (!value.isArray())
        return false;
    const auto a = value.toArray();
    if (a.size() != 4 || !number(a[0], 0, width) || !number(a[1], 0, height) ||
        !number(a[2], .1, 2000) || !number(a[3], .1, 2000))
        return false;
    out = {a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble()};
    return out.right() <= width + .000001 && out.bottom() <= height + .000001;
}
void issue(Review &result, QString code, QString label) {
    for (const auto &row : result.issues)
        if (row.toMap()["code"] == code)
            return;
    result.issues.append(QVariantMap{{"code", code}, {"label", label}});
}
QPainterPath worldPath(const Artwork &art, QString &error) {
    auto source = art;
    if (source.kind == "svg") {
        source.asset = source.asset ? SvgAsset::parse(source.asset->bytes, error) : nullptr;
        if (!source.asset) {
            if (error.isEmpty())
                error = "SVG-Geometrie fehlt.";
            return {};
        }
    }
    return JobPlanner::geometry(source, error, false);
}
QRectF padded(QRectF bounds) {
    if (bounds.width() < .1) {
        bounds.setX(bounds.x() - (.1 - bounds.width()) / 2);
        bounds.setWidth(.1);
    }
    if (bounds.height() < .1) {
        bounds.setY(bounds.y() - (.1 - bounds.height()) / 2);
        bounds.setHeight(.1);
    }
    return bounds;
}
} // namespace
Review review(const QList<Artwork> &items) {
    Review result;
    if (items.isEmpty() || items.size() > maximumItems) {
        result.error = "Sichtbare Auswahl mit 1–2000 Objekten erforderlich.";
        return result;
    }
    const int page = items.first().page;
    QSet<QString> ids, closedGroups;
    QHash<QString, QString> groupNames;
    QString previousGroup;
    int elements = 0;
    qint64 pixels = 0, svgWorkBytes = 0;
    for (const auto &art : items) {
        if (page < 0 || page >= 100 || !art.visible || art.page != page || art.id.isEmpty() ||
            ids.contains(art.id) || !Geometry::validBounds(art.bounds) || art.toolLayer < 0 ||
            art.toolLayer > 2) {
            result.error = "Auswahl enthält unsichtbare, seitenfremde oder inkonsistente Objekte; "
                           "kein Teilimport.";
            return result;
        }
        ids.insert(art.id);
        if (art.groupId != previousGroup) {
            if (!previousGroup.isEmpty())
                closedGroups.insert(previousGroup);
            if (!art.groupId.isEmpty() && closedGroups.contains(art.groupId)) {
                result.error = "Nicht zusammenhängende Quellgruppe; kein Teilimport.";
                return result;
            }
            previousGroup = art.groupId;
        }
        if ((!art.groupId.isEmpty() && art.groupName.trimmed().isEmpty()) ||
            (art.groupId.isEmpty() && !art.groupName.isEmpty()) ||
            (groupNames.contains(art.groupId) && groupNames[art.groupId] != art.groupName)) {
            result.error = "Inkonsistente Quellgruppe; kein Teilimport.";
            return result;
        }
        if (!art.groupId.isEmpty())
            groupNames[art.groupId] = art.groupName;
        if (!QStringList{"path", "rect", "ellipse", "text", "svg", "image"}.contains(art.kind)) {
            result.error = "Objekttyp wird in Grafikbibliothek Version 1 nicht unterstützt; Relief "
                           "wird nicht stillschweigend umgewandelt.";
            return result;
        }
        elements += art.path.elementCount();
        if (elements > maximumElements || art.text.size() > 2048 ||
            (art.asset && art.asset->bytes.size() > 1024 * 1024)) {
            result.error = "Grafik überschreitet Geometrie-Arbeitsgrenzen.";
            return result;
        }
        if (art.kind == "path" && art.path.isEmpty()) {
            result.error = "Leere Pfadkontur; kein Teilimport.";
            return result;
        }
        if (art.kind == "svg") {
            svgWorkBytes += art.asset ? art.asset->bytes.size() : 0;
            if (svgWorkBytes > 8 * 1024 * 1024) {
                result.error = "Auswahl überschreitet 8 MiB SVG-Verarbeitungsarbeit.";
                return result;
            }
            QString error;
            if (!art.asset || !SvgAsset::parse(art.asset->bytes, error)) {
                result.error = "SVG enthält ungültige oder externe Ressourcen; kein Teilimport.";
                return result;
            }
        }
        if (art.kind != "image") {
            result.conversionRequired = true;
            issue(result, "contour-appearance-only",
                  "Nur Konturgeometrie: Füllung, Strichfarbe und Strichbreite werden nicht "
                  "übernommen. Dies ist keine visuell identische Kopie.");
        }
        if (art.kind == "text") {
            result.conversionRequired = true;
            issue(result, "text-to-contours",
                  "Text wird zu gespeicherten Konturen und ist danach nicht mehr als Text "
                  "editierbar.");
        }
        if (art.kind == "svg") {
            result.conversionRequired = true;
            issue(result, "svg-to-contours",
                  "SVG wird als Konturgeometrie gespeichert; Farben, Pinsel und SVG-Editierbarkeit "
                  "werden nicht übernommen.");
        }
        if (art.kind == "rect" || art.kind == "ellipse")
            issue(result, "primitive-to-path",
                  "Grundformen werden als editierbare Pfadkonturen gespeichert.");
        if (art.kind == "image") {
            if (!art.reliefAsset || art.reliefAsset->image.isNull()) {
                result.error = "Bilddaten fehlen; kein Teilimport.";
                return result;
            }
            const auto size = art.reliefAsset->image.size();
            pixels += qint64(size.width()) * size.height();
            QString error;
            if (size.width() > 4096 || size.height() > 4096 || pixels > maximumPixels ||
                !ImageProcessing::validate(size, art.style, error)) {
                result.error = "Bilddaten oder Darstellungsoptionen überschreiten Grafikgrenzen.";
                return result;
            }
            result.conversionRequired = true;
            issue(result, "image-appearance-baked",
                  "Bilddarstellung wird einschließlich Zuschnitt, Maske und Rasterung in PNG "
                  "gebacken. Farbe, Originalpixel und live änderbare Filter gehen verloren.");
        }
    }
    QHash<QString, int> selectedGroupCounts;
    for (const auto &art : items)
        if (!art.groupId.isEmpty())
            ++selectedGroupCounts[art.groupId];
    for (const int count : selectedGroupCounts)
        if (count == 1) {
            issue(result, "single-member-ungrouped",
                  "Einzeln gewählte Gruppenmitglieder werden ohne Verknüpfung zur Quellgruppe "
                  "gespeichert.");
            break;
        }
    issue(result, "processing-discarded",
          "Geräte-, Material-, Leistungs-, Vorschub-, Durchgangs- und Operationsdaten werden nicht "
          "gespeichert. Einfügen aktiviert keine Ausgabe.");
    return result;
}
Encoded encode(const QList<Artwork> &items, bool accepted, const Cancellation &cancel) {
    Encoded result;
    if (isCancelled(cancel)) {
        result.error = "Grafikübernahme abgebrochen.";
        return result;
    }
    result.review = review(items);
    const auto fail = [&](QString message) {
        result.bytes.clear();
        result.contentHash.clear();
        result.error = std::move(message);
        return result;
    };
    if (!result.review.valid())
        return fail(result.review.error);
    if (result.review.conversionRequired && !accepted)
        return fail("Konvertierungsverluste zuerst ausdrücklich bestätigen.");
    QHash<QString, int> groupCounts, groups;
    for (const auto &art : items)
        if (!art.groupId.isEmpty())
            ++groupCounts[art.groupId];
    QList<QPainterPath> paths;
    QList<std::shared_ptr<const ReliefAsset>> images;
    QRectF unionBounds;
    bool first = true;
    int elements = 0;
    for (const auto &art : items) {
        if (isCancelled(cancel))
            return fail("Grafikübernahme abgebrochen.");
        QPainterPath path;
        std::shared_ptr<const ReliefAsset> image;
        QRectF bounds;
        if (art.kind == "image") {
            QString error;
            auto baked = ImageProcessing::apply(art.reliefAsset->image, art.style, error, cancel);
            if (baked.isNull())
                return fail("Bilddarstellung konnte nicht vollständig gebacken werden.");
            baked.setDevicePixelRatio(1);
            // Copy pixels into a fresh image: source PNG text is not artwork metadata.
            QImage clean(baked.size(), QImage::Format_ARGB32);
            if (clean.isNull())
                return fail("Bildspeicher für Grafikübernahme nicht verfügbar.");
            clean.fill(Qt::transparent);
            {
                QPainter copy(&clean);
                copy.setCompositionMode(QPainter::CompositionMode_Source);
                copy.drawImage(QPointF(), baked);
            }
            if (!(image = ReliefAsset::fromImage(clean)))
                return fail("Bilddarstellung konnte nicht vollständig gebacken werden.");
            bounds = art.bounds;
        } else {
            QString error;
            path = worldPath(art, error);
            elements += path.elementCount();
            if (!error.isEmpty() || path.isEmpty() || elements > maximumElements)
                return fail("Vektorgeometrie fehlt, ist nicht unterstützt oder zu komplex.");
            bounds = padded(path.boundingRect());
        }
        if (!std::isfinite(bounds.x()) || !std::isfinite(bounds.y()) ||
            !std::isfinite(bounds.width()) || !std::isfinite(bounds.height()) ||
            std::abs(bounds.x()) > 10000 || std::abs(bounds.y()) > 10000)
            return fail("Ungültige Geometriekoordinaten.");
        unionBounds = first ? bounds
                            : QRectF(QPointF(std::min(unionBounds.left(), bounds.left()),
                                             std::min(unionBounds.top(), bounds.top())),
                                     QPointF(std::max(unionBounds.right(), bounds.right()),
                                             std::max(unionBounds.bottom(), bounds.bottom())));
        first = false;
        paths.append(path);
        images.append(image);
    }
    unionBounds = padded(unionBounds);
    if (!Geometry::validBounds(unionBounds))
        return fail("Grafikabmessungen überschreiten 2000 mm.");
    QJsonArray encoded;
    for (int i = 0; i < items.size(); ++i) {
        if (isCancelled(cancel))
            return fail("Grafikübernahme abgebrochen.");
        const auto &art = items[i];
        int group = 0;
        if (groupCounts.value(art.groupId) > 1) {
            if (!groups.contains(art.groupId))
                groups[art.groupId] = groups.size() + 1;
            group = groups[art.groupId];
        }
        QJsonObject row{{"index", i},
                        {"group", group},
                        {"role", art.toolLayer ? "T" + QString::number(art.toolLayer) : "graphic"}};
        if (images[i]) {
            row["kind"] = "png";
            row["boundsMm"] = rect(art.bounds.translated(-unionBounds.topLeft()));
            row["png"] = QString::fromLatin1(images[i]->png.toBase64());
        } else {
            row["kind"] = "path";
            row["fillRule"] = int(paths[i].fillRule());
            row["path"] = VectorUtil::encode(
                QTransform::fromTranslate(-unionBounds.x(), -unionBounds.y()).map(paths[i]));
        }
        encoded.append(row);
    }
    const QJsonObject object{{"format", "blazer-geometry-asset"},
                             {"version", 1},
                             {"units", "mm"},
                             {"origin", "bottom-left"},
                             {"widthMm", unionBounds.width()},
                             {"heightMm", unionBounds.height()},
                             {"items", encoded}};
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    const auto valid = decode(bytes, cancel);
    if (!valid.valid())
        return fail(valid.error);
    result.bytes = valid.canonicalBytes;
    result.contentHash = valid.contentHash;
    return result;
}
Decoded decode(const QByteArray &bytes, const Cancellation &cancel) {
    Decoded result;
    const auto fail = [&](QString message) {
        Decoded rejected;
        rejected.error = std::move(message);
        return rejected;
    };
    if (isCancelled(cancel))
        return fail("Grafiklesen abgebrochen.");
    if (bytes.isEmpty() || bytes.size() > maximumBytes)
        return fail("Grafikdaten leer oder größer als 16 MiB.");
    const auto root = QJsonDocument::fromJson(bytes).object();
    if (!keys(root, {"format", "version", "units", "origin", "widthMm", "heightMm", "items"}) ||
        root["format"] != "blazer-geometry-asset" || root["version"] != 1 ||
        root["units"] != "mm" || root["origin"] != "bottom-left" ||
        !number(root["widthMm"], .1, 2000) || !number(root["heightMm"], .1, 2000) ||
        !root["items"].isArray())
        return fail("Ungültiges Grafikassetschema.");
    const double width = root["widthMm"].toDouble(), height = root["heightMm"].toDouble();
    const auto rows = root["items"].toArray();
    if (rows.isEmpty() || rows.size() > maximumItems)
        return fail("Ungültige Grafikobjektanzahl.");
    QHash<int, QString> groups;
    QHash<int, int> groupCounts;
    QSet<int> closedGroups;
    int previousGroup = 0;
    for (int i = 0; i < rows.size(); ++i) {
        if (isCancelled(cancel))
            return fail("Grafiklesen abgebrochen.");
        if (!rows[i].isObject())
            return fail("Ungültiges Grafikobjekt.");
        const auto row = rows[i].toObject();
        const auto kind = row["kind"].toString();
        const QStringList base{"index", "group", "role", "kind"};
        QStringList expected = base;
        expected +=
            (kind == "path" ? QStringList{"fillRule", "path"} : QStringList{"boundsMm", "png"});
        if (!keys(row, expected) || row["index"] != i || !number(row["group"], 0, maximumItems) ||
            std::floor(row["group"].toDouble()) != row["group"].toDouble() ||
            !row["role"].isString() ||
            !QStringList{"graphic", "T1", "T2"}.contains(row["role"].toString()))
            return fail("Unbekannte oder inkonsistente Grafikfelder.");
        Artwork art;
        art.id = uuid();
        art.name = "Bibliotheksgrafik";
        art.visible = true;
        art.layer = 0;
        art.toolLayer = row["role"] == "T2" ? 2 : 1;
        const int group = row["group"].toInt();
        if (group != previousGroup) {
            if (previousGroup)
                closedGroups.insert(previousGroup);
            if (group && closedGroups.contains(group))
                return fail("Nicht zusammenhängende Grafikgruppe.");
            previousGroup = group;
        }
        if (group && !groups.contains(group) && group != groups.size() + 1)
            return fail("Nicht kanonischer Grafikgruppenindex.");
        if (group) {
            if (!groups.contains(group))
                groups[group] = uuid();
            art.groupId = groups[group];
            art.groupName = "Grafikgruppe";
            ++groupCounts[group];
        }
        if (kind == "path") {
            QPainterPath world;
            if (!row["path"].isArray() || !VectorUtil::decode(row["path"].toArray(), world) ||
                world.isEmpty() || (row["fillRule"] != 0 && row["fillRule"] != 1))
                return fail("Ungültige Pfadkonturen.");
            result.elementCount += world.elementCount();
            if (result.elementCount > maximumElements)
                return fail("Grafik überschreitet 100000 Pfadelemente.");
            const auto pathBounds = world.boundingRect();
            if (pathBounds.left() < -.000001 || pathBounds.top() < -.000001 ||
                pathBounds.right() > width + .000001 || pathBounds.bottom() > height + .000001)
                return fail("Pfad außerhalb gespeicherter Grafikabmessungen.");
            world.setFillRule(Qt::FillRule(row["fillRule"].toInt()));
            art.kind = "path";
            art.bounds = padded(world.boundingRect());
            QTransform transform;
            transform.scale(1 / art.bounds.width(), 1 / art.bounds.height());
            transform.translate(-art.bounds.x(), -art.bounds.y());
            art.path = transform.map(world);
        } else if (kind == "png") {
            if (!rectangle(row["boundsMm"], art.bounds, width, height) || !row["png"].isString())
                return fail("Ungültige Bildgeometrie.");
            const auto base64 = row["png"].toString().toLatin1();
            const auto png =
                QByteArray::fromBase64Encoding(base64, QByteArray::AbortOnBase64DecodingErrors);
            if (!png || png.decoded.toBase64() != base64 ||
                !(art.reliefAsset = ReliefAsset::parse(png.decoded)))
                return fail("Ungültiges eingebettetes PNG.");
            if (!art.reliefAsset->image.textKeys().isEmpty())
                return fail("Nicht unterstützte PNG-Textmetadaten.");
            result.pixelCount +=
                qint64(art.reliefAsset->image.width()) * art.reliefAsset->image.height();
            if (result.pixelCount > maximumPixels)
                return fail("Grafik überschreitet 32 MiPixel.");
            art.kind = "image";
            art.style = {{"imageMode", "grayscale"},
                         {"brightness", 0.},
                         {"contrast", 1.},
                         {"gamma", 1.}}; // Baked pixels, no source filter state.
        } else
            return fail("Unbekannter Grafiktyp.");
        result.sourceRoles.append(row["role"].toString());
        result.items.append(art);
    }
    for (const int count : groupCounts)
        if (count < 2)
            return fail("Einzelobjekt mit ungültiger Gruppenzuordnung.");
    result.boundsMm = {0, 0, width, height};
    result.canonicalBytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    result.contentHash =
        QCryptographicHash::hash(result.canonicalBytes, QCryptographicHash::Sha256).toHex();
    return result;
}
QByteArray thumbnail(const QByteArray &bytes, QString &error, const Cancellation &cancel) {
    const auto decoded = decode(bytes, cancel);
    if (!decoded.valid()) {
        error = decoded.error;
        return {};
    }
    if (decoded.items.size() > 256 || decoded.elementCount > 20000) {
        error = "Grafik für begrenzte Vorschau zu komplex.";
        return {};
    }
    QImage image(256, 256, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto bounds = decoded.boundsMm;
    const double scale = 232 / std::max(bounds.width(), bounds.height());
    painter.translate(128, 128);
    painter.scale(scale, -scale);
    painter.translate(-bounds.width() / 2, -bounds.height() / 2);
    QPen pen(Qt::black);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    for (const auto &art : decoded.items) {
        if (isCancelled(cancel)) {
            error = "Vorschau abgebrochen.";
            return {};
        }
        if (art.kind == "path") {
            QTransform transform;
            transform.translate(art.bounds.x(), art.bounds.y());
            transform.scale(art.bounds.width(), art.bounds.height());
            painter.drawPath(transform.map(art.path));
        } else {
            painter.save();
            painter.translate(art.bounds.left(), art.bounds.bottom());
            painter.scale(art.bounds.width() / art.reliefAsset->image.width(),
                          -art.bounds.height() / art.reliefAsset->image.height());
            painter.drawImage(QPointF(), art.reliefAsset->image);
            painter.restore();
        }
    }
    painter.end();
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG") || png.size() > 256 * 1024) {
        error = "Vorschaubild konnte nicht begrenzt gespeichert werden.";
        return {};
    }
    error.clear();
    return png;
}
} // namespace GeometryAssetCodec
