#include "document.h"
#include "geometry.h"
#include "imagelimits.h"
#include "vectorutil.h"
#include <QClipboard>
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QMimeData>
#include <QUuid>
#include <QXmlStreamWriter>
#include <algorithm>

namespace {
constexpr auto mime = "application/x-blazer-objects-v1";
QString freshId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
QClipboard *clipboard() {
    return qobject_cast<QGuiApplication *>(QCoreApplication::instance())
               ? QGuiApplication::clipboard()
               : nullptr;
}
bool boundedWork(const QList<Artwork> &items) {
    qint64 elements = 0;
    for (const auto &item : items) {
        elements += item.path.elementCount();
        if (elements > 1000000)
            return false;
    }
    return items.size() <= Geometry::objectLimit;
}
QByteArray vectorSvg(const QList<Artwork> &items, const QVariantList &layers) {
    QList<QPainterPath> paths;
    qint64 elements = 0;
    QRectF bounds;
    bool first = true;
    for (const auto &a : items) {
        if (a.reliefAsset)
            return {};
        QString error;
        auto path = JobPlanner::geometry(a, error, true);
        if (!error.isEmpty() || path.isEmpty())
            return {};
        elements += path.elementCount();
        if (elements > 100000)
            return {};
        const auto r = path.boundingRect();
        bounds = first ? r
                       : QRectF(QPointF(std::min(bounds.left(), r.left()),
                                        std::min(bounds.top(), r.top())),
                                QPointF(std::max(bounds.right(), r.right()),
                                        std::max(bounds.bottom(), r.bottom())));
        first = false;
        paths.append(path);
    }
    if (first)
        return {};
    bounds = bounds.adjusted(-.05, -.05, .05, .05);
    QByteArray bytes;
    QXmlStreamWriter out(&bytes);
    out.writeStartDocument();
    out.writeStartElement("svg");
    out.writeDefaultNamespace("http://www.w3.org/2000/svg");
    const auto value = [](double n) { return QString::number(n, 'g', 17); };
    out.writeAttribute("width", value(bounds.width()) + "mm");
    out.writeAttribute("height", value(bounds.height()) + "mm");
    out.writeAttribute("viewBox", "0 0 " + value(bounds.width()) + " " + value(bounds.height()));
    QTransform transform(1, 0, 0, -1, -bounds.left(), bounds.bottom());
    for (int i = 0; i < paths.size(); ++i) {
        out.writeStartElement("path");
        if (!items[i].visible)
            out.writeAttribute("display", "none");
        out.writeAttribute("d", VectorUtil::svg(transform.map(paths[i])));
        out.writeAttribute("fill",
                           layers[items[i].layer].toMap()["mode"] == "Schnitt" ? "none" : "black");
        out.writeAttribute("fill-rule",
                           paths[i].fillRule() == Qt::OddEvenFill ? "evenodd" : "nonzero");
        out.writeAttribute("stroke", "black");
        out.writeAttribute("stroke-width", "0.1");
        out.writeEndElement();
    }
    out.writeEndElement();
    out.writeEndDocument();
    return bytes;
}
} // namespace

bool Document::copySelectionToClipboard(bool cut) {
    auto *board = clipboard();
    if (!board || m_selection.isEmpty()) {
        notify("Zum Kopieren zuerst Objekte auswählen.");
        return false;
    }
    finishMove();
    auto state = snapshot();
    state.items.clear();
    state.selection.clear();
    state.pages = {"Zwischenablage"};
    state.activePage = 0;
    state.title = "Zwischenablage";
    state.notes.clear();
    state.material.clear();
    state.layers = m_layers.mid(0, 2);
    QMap<int, int> operationMap{{0, 0}, {1, 1}};
    const auto operation = [&](int source) {
        if (!operationMap.contains(source)) {
            operationMap[source] = state.layers.size();
            state.layers.append(m_layers[source]);
        }
        return operationMap[source];
    };
    QMap<QString, int> groupCounts;
    for (int row : m_selection) {
        auto a = m_items[row];
        a.page = 0;
        a.layer = operation(a.layer);
        for (auto &extra : a.extraOperations)
            extra = operation(extra);
        ++groupCounts[a.groupId];
        state.items.append(a);
    }
    for (auto &a : state.items)
        if (groupCounts[a.groupId] < 2) {
            a.groupId.clear();
            a.groupName.clear();
        }
    for (int i = 0; i < state.items.size(); ++i)
        state.selection.append(i);
    state.selected = 0;
    if (!boundedWork(state.items) || !validBudget(state.items)) {
        notify("Auswahl überschreitet Zwischenablage-Arbeits- oder Datenlimits.");
        return false;
    }
    const auto bytes = serialize(state);
    if (bytes.size() > ImageLimits::projectBytes || !parse(bytes)) {
        notify("Auswahl überschreitet Zwischenablagegrenzen.");
        return false;
    }
    auto *data = new QMimeData;
    data->setData(mime, bytes);
    const auto svg = vectorSvg(state.items, state.layers);
    if (!svg.isEmpty() && svg.size() <= 8 * 1024 * 1024)
        data->setData("image/svg+xml", svg);
    if (state.items.size() == 1) {
        const auto &a = state.items.first();
        if (a.kind == "text")
            data->setText(a.text);
        if (a.kind == "image" && a.reliefAsset) {
            auto image = a.reliefAsset->image;
            image.setDevicePixelRatio(1);
            data->setImageData(image);
            data->setData("image/png", a.reliefAsset->png);
        }
    }
    board->setMimeData(data);
    const auto *stored = board->mimeData();
    if (!stored || stored->data(mime) != bytes) {
        notify("Zwischenablage konnte nicht übernommen werden; Auswahl bleibt erhalten.");
        return false;
    }
    if (cut)
        removeSelection("Ausschneiden");
    notify(cut ? "Auswahl ausgeschnitten; mit Einfügen wieder einsetzen."
               : "Auswahl in Systemzwischenablage kopiert.");
    return true;
}

bool Document::pasteFromClipboard(bool inPlace) {
    auto *board = clipboard();
    const auto *data = board ? board->mimeData() : nullptr;
    if (!data) {
        notify("Zwischenablage ist nicht verfügbar.");
        return false;
    }
    auto proposed = snapshot();
    QList<Artwork> items;
    QByteArray fingerprint;
    bool native = false;
    const auto fail = [this](const QString &error) {
        notify(error);
        return false;
    };
    if (data->hasFormat(mime)) {
        native = true;
        const auto bytes = data->data(mime);
        fingerprint = bytes;
        if (bytes.size() > ImageLimits::projectBytes)
            return fail("Zwischenablage größer als 64 MiB.");
        auto source = parse(bytes);
        if (!source || source->items.isEmpty())
            return fail("Ungültige Blazer-Zwischenablage; nichts eingefügt.");
        QMap<int, int> operations;
        const auto remap = [&](int op) {
            if (!operations.contains(op)) {
                const auto settings = source->layers[op];
                int target = proposed.layers.indexOf(settings);
                if (target < 0) {
                    target = proposed.layers.size();
                    proposed.layers.append(settings);
                }
                operations[op] = target;
            }
            return operations[op];
        };
        QMap<QString, QString> groups;
        for (auto a : source->items) {
            a.id = freshId();
            a.page = m_activePage;
            a.layer = remap(a.layer);
            for (auto &op : a.extraOperations)
                op = remap(op);
            if (!a.groupId.isEmpty()) {
                if (!groups.contains(a.groupId))
                    groups[a.groupId] = freshId();
                a.groupId = groups[a.groupId];
            }
            items.append(a);
        }
    } else {
        Artwork a;
        a.id = freshId();
        a.page = m_activePage;
        a.name = "Zwischenablage";
        QByteArray svg;
        if (data->hasFormat("image/svg+xml")) {
            svg = data->data("image/svg+xml");
            if (svg.isEmpty())
                return fail("Leere SVG-Zwischenablage; nichts eingefügt.");
        } else if (data->hasText()) {
            const auto text = data->text().trimmed();
            if (text.startsWith("<svg") ||
                (text.startsWith("<?xml") && text.left(1024).contains("<svg")))
                svg = text.toUtf8();
        }
        if (!svg.isEmpty()) {
            QString error;
            auto asset = SvgAsset::parse(svg, error);
            if (!asset)
                return fail(error);
            a.kind = "svg";
            a.asset = asset;
            a.layer = 0;
            a.bounds = QRectF(QPointF((Geometry::bedWidth - asset->sizeMm.width()) / 2,
                                      (Geometry::bedHeight - asset->sizeMm.height()) / 2),
                              asset->sizeMm);
            fingerprint = svg;
        } else if (data->hasImage() || data->hasFormat("image/png")) {
            const auto png = data->hasImage() ? QByteArray() : data->data("image/png");
            if (!data->hasImage() && (png.size() > ImageLimits::embeddedBytes ||
                                      !png.startsWith(QByteArray::fromHex("89504e470d0a1a0a"))))
                return fail("Ungültige PNG-Daten in der Zwischenablage.");
            auto asset = data->hasImage()
                             ? ReliefAsset::fromImage(qvariant_cast<QImage>(data->imageData()))
                             : ReliefAsset::parse(png);
            if (!asset)
                return fail("Bild in Zwischenablage ungültig oder zu groß.");
            a.kind = "image";
            a.reliefAsset = asset;
            a.style = {{"imageMode", "dither"}};
            const auto &image = asset->image;
            const double mm = std::min(.1, 100. / std::max(image.width(), image.height()));
            a.bounds = QRectF(10, 10, image.width() * mm, image.height() * mm);
            fingerprint = asset->png;
        } else if (data->hasText() && !data->hasUrls()) {
            const auto text = data->text();
            if (text.isEmpty() || text.size() > 1000)
                return fail("Zwischenablagetext muss 1–1000 Zeichen enthalten.");
            a.kind = "text";
            a.text = text;
            a.bounds = QRectF(10, 10, 80, 20);
            fingerprint = text.toUtf8();
        } else
            return fail("Keine unterstützten Blazer-Objekte, SVG-, Bild- oder Textdaten in der "
                        "Zwischenablage.");
        a.layer = -1;
        for (int i = 0; i < proposed.layers.size(); ++i)
            if (proposed.layers[i].toMap()["mode"] == "Gravur") {
                a.layer = i;
                break;
            }
        if (a.layer < 0)
            return fail("Für externe Zwischenablageinhalte zuerst eine Gravuroperation anlegen.");
        items.append(a);
    }
    const auto hash = QCryptographicHash::hash(fingerprint, QCryptographicHash::Sha256);
    const int count = hash == m_clipboardPasteHash && m_clipboardPastePage == m_activePage
                          ? m_clipboardPasteCount
                          : 0;
    const double offset = inPlace ? 0 : 5. * (count + (native ? 1 : 0));
    for (auto &a : items)
        a.bounds.translate(offset, offset);
    const int first = proposed.items.size();
    proposed.items.append(items);
    proposed.selected = first;
    proposed.selection.clear();
    for (int i = first; i < proposed.items.size(); ++i)
        proposed.selection.append(i);
    if (!boundedWork(proposed.items) || !validBudget(proposed.items) || proposed.layers.size() > 64)
        return fail("Einfügen überschreitet Arbeits- oder Datenlimits; nichts eingefügt.");
    auto valid = parse(serialize(proposed));
    if (!valid)
        return fail("Einfügen überschreitet Objekt-, Operations-, Koordinaten- oder Datenlimits; "
                    "nichts eingefügt.");
    checkpoint("Einfügen");
    applySnapshot(*valid);
    if (!inPlace) {
        m_clipboardPasteHash = hash;
        m_clipboardPastePage = m_activePage;
        m_clipboardPasteCount = count + 1;
    }
    notify(QString::number(items.size()) + " Objekte eingefügt" +
           (native ? "; zugeordnete Operationen erhalten. Vor Ausgabe prüfen."
                   : "; eigene Gravuroperation zugeordnet."));
    return true;
}
