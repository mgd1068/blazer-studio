#include "svgasset.h"
#include "geometry.h"
#include <QRegularExpression>
#include <QSvgRenderer>
#include <QXmlStreamReader>
#include <optional>

namespace {
std::optional<double> lengthMm(const QString &s) {
    static const QRegularExpression re(
        R"(^\s*((?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s*(mm|cm|in|px|pt|pc|q)?\s*$)",
        QRegularExpression::CaseInsensitiveOption);
    auto match = re.match(s);
    if (!match.hasMatch())
        return {};
    bool ok;
    double n = match.captured(1).toDouble(&ok);
    const auto unit = match.captured(2).toLower();
    if (unit == "cm")
        n *= 10;
    else if (unit == "in")
        n *= 25.4;
    else if (unit == "pt")
        n *= 25.4 / 72;
    else if (unit == "pc")
        n *= 25.4 / 6;
    else if (unit == "q")
        n *= .25;
    else if (unit == "px" || unit.isEmpty())
        n *= 25.4 / 96;
    if (!ok || !std::isfinite(n) || n <= 0)
        return {};
    return n;
}
bool externalCssReference(const QString &text) {
    static const QRegularExpression url(R"(url\s*\(\s*([^)]*)\))",
                                        QRegularExpression::CaseInsensitiveOption);
    if (text.contains("@import", Qt::CaseInsensitive))
        return true;
    auto matches = url.globalMatch(text);
    while (matches.hasNext()) {
        QString value = matches.next().captured(1).trimmed();
        value.remove('"');
        value.remove('\'');
        if (!value.startsWith('#'))
            return true;
    }
    return false;
}
} // namespace
std::shared_ptr<const SvgAsset> SvgAsset::parse(const QByteArray &bytes, QString &error) {
    auto fail = [&error](const QString &message) -> std::shared_ptr<const SvgAsset> {
        error = message;
        return {};
    };
    if (bytes.isEmpty() || bytes.size() > 8 * 1024 * 1024)
        return fail("SVG ist leer oder größer als 8 MB.");
    QXmlStreamReader xml(bytes);
    QString w, h;
    QSizeF viewBox;
    bool root = false;
    int elements = 0;
    while (!xml.atEnd()) {
        auto token = xml.readNext();
        if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference)
            return fail("SVG mit DTD oder Entities wird nicht unterstützt.");
        if (xml.isStartElement()) {
            if (++elements > 100000)
                return fail("SVG enthält zu viele Elemente.");
            if (!root) {
                if (xml.name() != u"svg")
                    return fail("Die Datei besitzt kein SVG-Wurzelelement.");
                root = true;
                w = xml.attributes().value("width").toString();
                h = xml.attributes().value("height").toString();
                if (xml.attributes().hasAttribute("viewBox")) {
                    auto values = xml.attributes().value("viewBox").toString().split(
                        QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts);
                    if (values.size() != 4)
                        return fail("Ungültige SVG-viewBox.");
                    QList<double> nums;
                    for (const auto &value : values) {
                        bool ok;
                        double n = value.toDouble(&ok);
                        if (!ok || !std::isfinite(n))
                            return fail("Ungültige SVG-viewBox.");
                        nums.append(n);
                    }
                    viewBox = {nums[2], nums[3]};
                    if (viewBox.width() <= 0 || viewBox.height() <= 0)
                        return fail("Ungültige SVG-viewBox.");
                }
            }
            if (xml.name() == u"script" || xml.name() == u"foreignObject" ||
                xml.name() == u"animate" || xml.name() == u"animateTransform" ||
                xml.name() == u"animateMotion" || xml.name() == u"set")
                return fail(
                    "SVG mit Skripten, Animationen oder Fremdinhalten wird nicht unterstützt.");
            for (const auto &a : xml.attributes()) {
                if ((a.name() == u"href" && !a.value().startsWith(u'#')) ||
                    externalCssReference(a.value().toString()))
                    return fail("SVG mit externen oder eingebetteten Dateiverweisen wird nicht "
                                "unterstützt.");
            }
        }
        if (xml.isCharacters() && externalCssReference(xml.text().toString()))
            return fail("SVG mit externen CSS-Verweisen wird nicht unterstützt.");
    }
    if (xml.hasError() || !root)
        return fail("Ungültiges SVG.");
    std::optional<double> width, height;
    if (!w.isEmpty()) {
        width = lengthMm(w);
        if (!width)
            return fail("SVG-Breite hat keine unterstützte absolute Einheit.");
    }
    if (!h.isEmpty()) {
        height = lengthMm(h);
        if (!height)
            return fail("SVG-Höhe hat keine unterstützte absolute Einheit.");
    }
    if (!width || !height) {
        if (viewBox.isEmpty())
            return fail("SVG benötigt eindeutige Maße oder eine viewBox.");
        if (width)
            height = *width * viewBox.height() / viewBox.width();
        else if (height)
            width = *height * viewBox.width() / viewBox.height();
        else {
            width = viewBox.width() * 25.4 / 96;
            height = viewBox.height() * 25.4 / 96;
        }
    }
    if (!Geometry::validBounds(QRectF(0, 0, *width, *height)))
        return fail("SVG-Abmessungen liegen außerhalb des unterstützten Bereichs.");
    auto asset = std::make_shared<SvgAsset>();
    asset->bytes = bytes;
    asset->sizeMm = {*width, *height};
    asset->renderer = std::make_shared<QSvgRenderer>();
    asset->renderer->setAnimationEnabled(false);
    if (!asset->renderer->load(bytes))
        return fail("SVG konnte nicht dargestellt werden.");
    asset->renderer->setAnimationEnabled(false);
    error.clear();
    return asset;
}
