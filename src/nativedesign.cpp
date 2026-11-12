#include "nativedesign.h"
#include "geometry.h"
#include "imagelimits.h"
#include <QDomDocument>
#include <QFont>
#include <QHash>
#include <QPainter>
#include <QSet>
#include <QTransform>
#include <QUuid>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>

namespace {
constexpr int geometryLimit = 100000;
class Numbers {
    const QString text;
    qsizetype offset = 0;

  public:
    explicit Numbers(QString value) : text(std::move(value)) {}
    void space() {
        while (offset < text.size() && text[offset].isSpace())
            ++offset;
    }
    bool done() {
        space();
        return offset == text.size();
    }
    bool take(QChar c) {
        space();
        if (offset < text.size() && text[offset] == c) {
            ++offset;
            return true;
        }
        return false;
    }
    bool number(double &value) {
        space();
        const auto start = offset;
        if (offset < text.size() && (text[offset] == '+' || text[offset] == '-'))
            ++offset;
        auto digit = [&] {
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
        if (!digits)
            return false;
        if (offset < text.size() && (text[offset] == 'e' || text[offset] == 'E')) {
            ++offset;
            if (offset < text.size() && (text[offset] == '+' || text[offset] == '-'))
                ++offset;
            bool exponent = false;
            while (digit()) {
                exponent = true;
                ++offset;
            }
            if (!exponent)
                return false;
        }
        if (offset - start > 64)
            return false;
        bool ok;
        value = text.mid(start, offset - start).toDouble(&ok);
        return ok && std::isfinite(value) && std::abs(value) <= 1e9;
    }
    bool separatedNumber(double &value) {
        if (offset >= text.size() || !text[offset].isSpace())
            return false;
        return number(value);
    }
    bool index(int &value) {
        space();
        const auto start = offset;
        while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9')
            ++offset;
        if (start == offset || offset - start > 6)
            return false;
        bool ok;
        value = text.mid(start, offset - start).toInt(&ok);
        return ok && value < geometryLimit;
    }
};

class Reader {
    NativeDesign::Result result;
    QHash<int, bool> visibleLayers;
    QSet<int> toolCuts;
    int shapes = 0, vertices = 0, elements = 0;
    qint64 pixels = 0, embedded = 0;
    bool millmageMode = false;
    bool fail(const QString &message) {
        result.error = message;
        return false;
    }
    bool scalar(const QDomElement &node, const QString &key, double &out, double fallback,
                bool required = false) {
        if (!node.hasAttribute(key)) {
            out = fallback;
            return !required || fail("Fehlendes Attribut " + key);
        }
        Numbers reader(node.attribute(key));
        return (reader.number(out) && reader.done()) || fail("Ungültiges Attribut " + key);
    }
    bool unique(const QDomElement &node, const QString &tag, QDomElement &child) {
        child = node.firstChildElement(tag);
        return child.isNull() || child.nextSiblingElement(tag).isNull() ||
               fail("Doppeltes Element " + tag);
    }
    bool matrix(const QDomElement &node, QTransform &out) {
        QDomElement child;
        if (!unique(node, "XForm", child))
            return false;
        if (child.isNull())
            return true;
        Numbers numbers(child.text());
        double values[6];
        for (double &value : values)
            if (!numbers.number(value) || std::abs(value) > 1e6)
                return fail("Ungültige Transformation");
        if (!numbers.done())
            return fail("Transformation benötigt sechs Werte");
        out = QTransform(values[0], values[1], values[2], values[3], values[4], values[5]);
        return true;
    }
    bool path(const QDomElement &node, QPainterPath &out) {
        QDomElement vert, prim;
        if (!unique(node, "VertList", vert) || !unique(node, "PrimList", prim))
            return false;
        if (vert.isNull() || prim.isNull())
            return fail("Pfad benötigt VertList und PrimList");
        struct Vertex {
            QPointF p, left, right;
        };
        QList<Vertex> points;
        Numbers input(vert.text());
        while (!input.done()) {
            double x, y;
            if (!input.take('V') || !input.number(x) || !input.separatedNumber(y))
                return fail("Ungültiger Pfadpunkt");
            Vertex v{{x, y}, {x, y}, {x, y}};
            bool seen[4]{};
            double controls[4]{};
            while (input.take('c')) {
                const int side = input.take('0') ? 0 : input.take('1') ? 1 : -1;
                const int axis = input.take('x') ? 0 : input.take('y') ? 1 : -1;
                if (side < 0 || axis < 0 || seen[side * 2 + axis] ||
                    !input.number(controls[side * 2 + axis]))
                    return fail("Ungültiger Bézierkontrollpunkt");
                seen[side * 2 + axis] = true;
            }
            for (int side = 0; side < 2; ++side) {
                const int x = side * 2, y = x + 1;
                if (seen[x] && seen[y]) {
                    (side == 0 ? v.left : v.right) = QPointF(controls[x], controls[y]);
                } else if (seen[x] || seen[y]) {
                    // The native writer's x=1 without y is the disabled-handle
                    // sentinel, not an absolute control point at x=1.
                    if (!seen[x] || seen[y] || controls[x] != 1)
                        return fail("Unbekannte Kontrollpunktkodierung");
                }
            }
            if (++vertices > geometryLimit)
                return fail("Zu viele Pfadpunkte");
            points.append(v);
        }
        Numbers commands(prim.text());
        int previous = -1;
        bool any = false;
        while (!commands.done()) {
            bool bezier = false;
            if (commands.take('B'))
                bezier = true;
            else if (!commands.take('L'))
                return fail("Unbekannte Pfadprimitive");
            int a, b;
            if (!commands.index(a) || !commands.index(b) || a >= points.size() ||
                b >= points.size())
                return fail("Ungültiger Primitivindex");
            elements += (a != previous ? 1 : 0) + (bezier ? 3 : 1);
            if (elements > geometryLimit)
                return fail("Zu viele Pfadelemente");
            if (a != previous)
                out.moveTo(points[a].p);
            if (bezier)
                out.cubicTo(points[a].right, points[b].left, points[b].p);
            else
                out.lineTo(points[b].p);
            previous = b;
            any = true;
        }
        return any || fail("Leerer Pfad");
    }
    bool bitmap(const QDomElement &node, const QTransform &transform,
                NativeDesign::Element &element) {
        const QStringList attributes{"Type",
                                     "OpIndex",
                                     "LayerIndex",
                                     "CutIndex",
                                     "W",
                                     "H",
                                     "Gamma",
                                     "Contrast",
                                     "Brightness",
                                     "EnhanceAmount",
                                     "EnhanceRadius",
                                     "EnhanceDenoise",
                                     "File",
                                     "SourceHash",
                                     "Data"};
        const auto sourceAttributes = node.attributes();
        for (int i = 0; i < sourceAttributes.size(); ++i)
            if (!attributes.contains(sourceAttributes.item(i).nodeName()))
                return fail("Nicht unterstützte Bildoption: " +
                            sourceAttributes.item(i).nodeName());
        double width, height;
        if (!scalar(node, "W", width, 0, true) || !scalar(node, "H", height, 0, true) ||
            width <= 0 || height <= 0)
            return fail("Ungültige Bildgröße");
        for (const auto &setting : QList<QPair<QString, double>>{{"Gamma", 1},
                                                                 {"Contrast", 0},
                                                                 {"Brightness", 0},
                                                                 {"EnhanceAmount", 0},
                                                                 {"EnhanceRadius", 0},
                                                                 {"EnhanceDenoise", 0}}) {
            double value;
            if (!scalar(node, setting.first, value, setting.second))
                return false;
            if (value != setting.second)
                return fail("Bildanpassung " + setting.first + " wird noch nicht unterstützt");
        }
        const auto decoded = QByteArray::fromBase64Encoding(
            node.attribute("Data").toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded)
            return fail("Ungültige eingebettete Bilddaten");
        if (!decoded.decoded.startsWith(QByteArray::fromHex("89504e470d0a1a0a")))
            return fail("Eingebettetes Bild ist kein PNG");
        auto original = ReliefAsset::parse(decoded.decoded);
        if (!original)
            return fail(
                "Eingebettetes PNG ungültig oder zu groß; externe Bilder werden nicht geladen");
        const auto &image = original->image;
        const auto bounds = transform.mapRect(QRectF(-width / 2, -height / 2, width, height));
        if (!Geometry::validBounds(bounds) || !transform.isAffine() || !transform.isInvertible())
            return fail("Ungültige Bildtransformation");
        // Native PNG rows increase with project Y (bottom-first storage).
        // Convert to the application's ordinary top-first raster convention.
        QTransform local(width / image.width(), 0, 0, height / image.height(), -width / 2,
                         -height / 2);
        const QTransform toWorld = local * transform;
        const double a = toWorld.m11(), b = toWorld.m21(), c = toWorld.m12(), d = toWorld.m22();
        const double determinant = a * d - b * c;
        if (!(std::abs(determinant) > 0))
            return fail("Entartete Bildtransformation");
        // Inverse-basis lengths preserve source resolution independently on
        // each world axis, including nonuniform scale and quarter rotations.
        const double densityX = std::hypot(d, c) / std::abs(determinant);
        const double densityY = std::hypot(b, a) / std::abs(determinant);
        const double outputWidth = std::ceil(bounds.width() * densityX - 1e-8);
        const double outputHeight = std::ceil(bounds.height() * densityY - 1e-8);
        if (!std::isfinite(outputWidth) || !std::isfinite(outputHeight) || outputWidth < 1 ||
            outputHeight < 1 || outputWidth > ImageLimits::side ||
            outputHeight > ImageLimits::side || outputWidth * outputHeight > ImageLimits::pixels)
            return fail("Transformiertes Bild überschreitet Pixelgrenzen");
        if (pixels + qint64(outputWidth) * qint64(outputHeight) > ImageLimits::documentPixels)
            return fail("Eingebettete Bilder überschreiten Projekt-Pixelgrenze");
        const QSize size{int(outputWidth), int(outputHeight)};
        QImage raster(size, image.depth() > 32 ? QImage::Format_RGBA64 : QImage::Format_ARGB32);
        if (raster.isNull())
            return fail("Bildspeicher nicht verfügbar");
        raster.fill(Qt::transparent);
        QTransform worldToRaster(size.width() / bounds.width(), 0, 0,
                                 -size.height() / bounds.height(),
                                 -bounds.left() * size.width() / bounds.width(),
                                 bounds.bottom() * size.height() / bounds.height());
        QPainter painter(&raster);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setTransform(toWorld * worldToRaster);
        painter.drawImage(QPointF(0, 0), image);
        painter.end();
        element.image = ReliefAsset::fromImage(raster);
        if (!element.image)
            return fail("Transformiertes Bild konnte nicht gespeichert werden");
        element.bounds = bounds;
        pixels += qint64(size.width()) * size.height();
        embedded += element.image->png.size();
        return (pixels <= ImageLimits::documentPixels && embedded <= ImageLimits::embeddedBytes) ||
               fail("Eingebettete Bilder überschreiten Projektgrenzen");
    }
    bool shape(const QDomElement &node, const QTransform &parent, const QString &group,
               bool inherited, int inheritedTool = 0) {
        if (++shapes > Geometry::objectLimit)
            return fail("Zu viele Formen/Gruppen");
        QTransform local;
        if (!matrix(node, local))
            return false;
        const QTransform combined = local * parent;
        for (double value : {combined.m11(), combined.m12(), combined.m21(), combined.m22(),
                             combined.dx(), combined.dy()})
            if (!std::isfinite(value) || std::abs(value) > 1e9)
                return fail("Kombinierte Transformation außerhalb des Bereichs");
        bool visible = inherited;
        if (node.hasAttribute("LayerIndex")) {
            double layer;
            if (!scalar(node, "LayerIndex", layer, 0) || std::floor(layer) != layer || layer < 0 ||
                layer > 255)
                return fail("Ungültiger Grafik-Layer");
            visible = visible && visibleLayers.value(int(layer), true);
        }
        int toolLayer = inheritedTool;
        if (!millmageMode && node.hasAttribute("CutIndex")) {
            double cut;
            if(!scalar(node,"CutIndex",cut,0) || cut<0 || cut>255 || std::floor(cut)!=cut)
                return fail("Ungültige LightBurn-Layerzuordnung");
            toolLayer = cut==31 ? 2 : (cut==30 || toolCuts.contains(int(cut))) ? 1 : 0;
        }
        const auto type = node.attribute("Type");
        if (type.size() > 64)
            return fail("Ungültiger Formtyp");
        QStringList allowed{"XForm"};
        if (type == "Group")
            allowed << "Children";
        else if (type == "Path")
            allowed << "VertList" << "PrimList";
        else if (type == "Text")
            allowed << "BackupPath";
        for (auto child = node.firstChildElement(); !child.isNull();
             child = child.nextSiblingElement())
            if (!allowed.contains(child.tagName()))
                return fail("Nicht unterstützter Forminhalt: " + child.tagName());
        if (type == "Group") {
            const auto id =
                group.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : group;
            QDomElement children;
            if (!unique(node, "Children", children) || children.isNull())
                return fail("Gruppe ohne Children");
            for (auto child = children.firstChildElement(); !child.isNull();
                 child = child.nextSiblingElement())
                if (child.tagName() != "Shape" || !shape(child, combined, id, visible, toolLayer))
                    return fail(result.error.isEmpty() ? "Unbekannter Gruppeninhalt"
                                                       : result.error);
            return true;
        }
        if (type == "Text") {
            QDomElement backup;
            if (!unique(node, "BackupPath", backup))
                return false;
            const auto outline = backup.firstChildElement("Shape");
            if (outline.isNull()) {
                if (millmageMode)
                    return fail("MillMage-Text benötigt gespeicherte Konturen");
                if (node.attribute("Str").size() > 2048 || node.attribute("Font").size() > 1000)
                    return fail("LightBurn-Text überschreitet Importgrenzen");
                // Preserve the previously documented legacy LightBurn fallback.
                QFont font(node.attribute("Font").section(',', 0, 0));
                font.setPixelSize(100);
                QPainterPath text;
                text.addText(0, 0, font, node.attribute("Str"));
                double height;
                if (!scalar(node, "H", height, 0, true) || height <= 0 ||
                    text.boundingRect().isEmpty())
                    return fail("Ungültiger LightBurn-Text");
                QTransform scale;
                scale.scale(height / text.boundingRect().height(),
                            height / text.boundingRect().height());
                text = (scale * combined).map(text);
                elements += text.elementCount();
                if (elements > geometryLimit)
                    return fail("Zu viele Textkonturen");
                result.elements.append({text, {}, {}, group, visible, toolLayer});
                return true;
            }
            if (!outline.nextSiblingElement().isNull())
                return fail("Mehrdeutige Textkonturen");
            const int first=result.elements.size();
            if(!shape(outline, combined, group, visible, toolLayer))return false;
            for(int i=first;i<result.elements.size();++i)result.elements[i].toolLayer=toolLayer;
            return true;
        }
        NativeDesign::Element element;
        element.group = group;
        element.visible = visible;
        element.toolLayer = toolLayer;
        if (type == "Bitmap") {
            if (!bitmap(node, combined, element))
                return false;
        } else {
            QPainterPath geometry;
            if (type == "Rect") {
                double w, h, r;
                if (!scalar(node, "W", w, 0, true) || !scalar(node, "H", h, 0, true) ||
                    !scalar(node, "Cr", r, 0) || w <= 0 || h <= 0 || r < 0)
                    return fail("Ungültiges Rechteck");
                geometry.addRoundedRect(QRectF(-w / 2, -h / 2, w, h), r, r);
                elements += geometry.elementCount();
            } else if (type == "Ellipse") {
                double rx, ry;
                if (!scalar(node, "Rx", rx, 0, true) || !scalar(node, "Ry", ry, 0, true) ||
                    rx <= 0 || ry <= 0)
                    return fail("Ungültige Ellipse");
                geometry.addEllipse(QRectF(-rx, -ry, 2 * rx, 2 * ry));
                elements += geometry.elementCount();
            } else if (type == "Path") {
                if (!path(node, geometry))
                    return false;
            } else
                return fail("Nicht unterstützte Form: " + type);
            if (elements > geometryLimit)
                return fail("Zu viele Pfadelemente");
            element.path = combined.map(geometry);
        }
        result.elements.append(element);
        return true;
    }

  public:
    NativeDesign::Result run(const QByteArray &bytes, bool millmage) {
        millmageMode = millmage;
        if (bytes.size() > 16 * 1024 * 1024)
            return {{}, "Native Projektdatei größer als 16 MiB"};
        QXmlStreamReader check(bytes);
        int depth = 0, nodes = 0;
        while (!check.atEnd()) {
            check.readNext();
            if (check.isDTD())
                return {{}, "DTD in nativer Projektdatei abgelehnt"};
            if (check.isStartElement() && (++depth > 64 || ++nodes > 100000))
                return {{}, "XML zu tief oder zu viele Elemente"};
            if (check.isEndElement())
                --depth;
        }
        if (check.hasError())
            return {{}, "Ungültiges Projekt-XML"};
        QDomDocument document;
        if (!document.setContent(bytes))
            return {{}, "Ungültiges Projekt-XML"};
        const auto root = document.documentElement();
        if (root.tagName() != (millmage ? "MillMageProject" : "LightBurnProject"))
            return {{}, "Dateiendung und Projektkennung passen nicht zusammen"};
        if (root.attribute("FormatVersion", millmage ? "" : "1") != "1")
            return {{}, "Nicht unterstützte native FormatVersion"};
        for (const QString key : {"MirrorX", "MirrorY"}) {
            const auto value = root.attribute(key, "False").toLower();
            if (value != "false" && value != "true")
                return {{}, "Ungültiger Spiegelungswert"};
            if (millmage && value == "true")
                return {{}, "Gespiegelter MillMage-Ursprung benötigt weitere Formatabnahme"};
        }
        for (auto layer = root.firstChildElement("Layer"); !layer.isNull();
             layer = layer.nextSiblingElement("Layer")) {
            double index, hide;
            QDomElement indexNode, hideNode;
            if (!unique(layer, "index", indexNode) || !unique(layer, "hide", hideNode))
                return {{}, result.error};
            if (!scalar(indexNode, "Value", index, 0, true) ||
                !scalar(hideNode, "Value", hide, 0) || index < 0 || index > 255 ||
                std::floor(index) != index || (hide != 0 && hide != 1) ||
                visibleLayers.contains(int(index)))
                return {{}, "Ungültige oder doppelte Grafik-Layer"};
            visibleLayers[int(index)] = hide == 0;
        }
        if (!millmage)
            for(auto cut=root.firstChildElement("CutSetting");!cut.isNull();cut=cut.nextSiblingElement("CutSetting"))
                if(cut.attribute("type").compare("Tool",Qt::CaseInsensitive)==0) {
                    QDomElement indexNode;double index;
                    if(!unique(cut,"index",indexNode) || !scalar(indexNode,"Value",index,0,true) || index<0 || index>255 || std::floor(index)!=index)
                        return {{},"Ungültige LightBurn-Werkzeugebene"};
                    toolCuts.insert(int(index));
                }
        const QStringList metadata{"Layer",        "Thumbnail",  "CNCProjectSetup",
                                   "VariableText", "UIPrefs",    "Operation",
                                   "Notes",        "CutSetting", "CutSetting_Img"};
        for (auto child = root.firstChildElement(); !child.isNull();
             child = child.nextSiblingElement())
            if (child.tagName() != "Shape" && !metadata.contains(child.tagName()) &&
                !child.elementsByTagName("Shape").isEmpty())
                return {{}, "Geometrie in nicht unterstütztem Projektcontainer"};
        for (auto node = root.firstChildElement("Shape"); !node.isNull();
             node = node.nextSiblingElement("Shape"))
            if (!shape(node, {}, {}, true))
                return {{}, result.error};
        if (result.elements.isEmpty())
            return {{}, "Keine importierbare native Geometrie"};
        return result;
    }
};
} // namespace
NativeDesign::Result NativeDesign::read(const QByteArray &bytes, bool millmage) {
    return Reader().run(bytes, millmage);
}
