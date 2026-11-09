#include "document.h"
#include <QFile>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSvgRenderer>
#include <QUuid>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>

namespace {
QString id() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
bool isFiniteNumber(double n) {
    return std::isfinite(n);
}
double physicalLength(const QString &s, double fallback) {
    static const QRegularExpression rx(R"(^\s*([\d.]+)\s*(mm|cm|in|px|pt)?\s*$)");
    const auto m = rx.match(s);
    if (!m.hasMatch())
        return fallback;
    double n = m.captured(1).toDouble();
    auto u = m.captured(2);
    if (u == "cm")
        n *= 10;
    else if (u == "in")
        n *= 25.4;
    else if (u == "pt")
        n *= 25.4 / 72;
    else if (u == "px" || u.isEmpty())
        n *= 25.4 / 96;
    return n > 0 && isFiniteNumber(n) ? n : fallback;
}
} // namespace
Document::Document(QObject *p) : QAbstractListModel(p) {
    demo();
}
int Document::rowCount(const QModelIndex &p) const {
    return p.isValid() ? 0 : m_items.size();
}
QVariant Document::data(const QModelIndex &i, int role) const {
    if (!i.isValid() || i.row() < 0 || i.row() >= m_items.size())
        return {};
    const auto &a = m_items[i.row()];
    switch (role) {
    case NameRole:
        return a.name;
    case KindRole:
        return a.kind;
    case ColorRole:
        return layerColor(a.layer);
    case VisibleRole:
        return a.visible;
    default:
        return {};
    }
}
QHash<int, QByteArray> Document::roleNames() const {
    return {{NameRole, "artName"},
            {KindRole, "artKind"},
            {ColorRole, "artColor"},
            {VisibleRole, "artVisible"}};
}
QColor Document::layerColor(int n) const {
    return n == 0 ? QColor("#cb713c") : QColor("#4c7bd9");
}
QVariantMap Document::selection() const {
    if (m_selected < 0 || m_selected >= m_items.size())
        return {};
    const auto &a = m_items[m_selected];
    return {{"name", a.name},
            {"kind", a.kind},
            {"text", a.text},
            {"x", a.bounds.x()},
            {"y", a.bounds.y()},
            {"width", a.bounds.width()},
            {"height", a.bounds.height()},
            {"layer", a.layer},
            {"visible", a.visible}};
}
QVariantList Document::layers() const {
    return m_layers;
}
void Document::select(int row) {
    if (row < -1 || row >= m_items.size())
        row = -1;
    if (row == m_selected)
        return;
    m_selected = row;
    emit selectionChanged();
}
void Document::notify(const QString &s) {
    m_message = s;
    emit messageChanged();
}
void Document::refresh() {
    beginResetModel();
    endResetModel();
    m_dirty = true;
    emit changed();
    emit selectionChanged();
}
void Document::demo() {
    beginResetModel();
    m_title = "Werkstattschild";
    m_layers = {QVariantMap{{"name", "01 · Gravieren"},
                            {"mode", "Gravur"},
                            {"speed", 3000.},
                            {"power", 20.},
                            {"passes", 1},
                            {"enabled", true}},
                QVariantMap{{"name", "02 · Schneiden"},
                            {"mode", "Schnitt"},
                            {"speed", 600.},
                            {"power", 80.},
                            {"passes", 1},
                            {"enabled", true}}};
    m_items = {
        {id(), "Schildkontur", "rect", "", QRectF(30, 35, 140, 80), 1, true, {}},
        {id(), "Bohrung links", "ellipse", "", QRectF(37, 42, 4, 4), 1, true, {}},
        {id(), "Bohrung rechts", "ellipse", "", QRectF(159, 42, 4, 4), 1, true, {}},
        {id(), "MOIN", "text", "MOIN", QRectF(51, 58, 98, 32), 0, true, {}},
        {id(), "Unterzeile", "text", "ZEIT FÜR GUTE IDEEN", QRectF(55, 95, 90, 7), 0, true, {}}};
    m_selected = 0;
    m_dirty = false;
    m_undo.clear();
    m_redo.clear();
    endResetModel();
    emit changed();
    emit selectionChanged();
    notify("Beispielprojekt · Parameter sind Entwurfswerte, keine Materialempfehlung.");
}
void Document::newProject() {
    demo();
    beginResetModel();
    m_items.clear();
    m_title = "Unbenannt";
    m_selected = -1;
    endResetModel();
    emit changed();
    emit selectionChanged();
    notify("Neues Projekt · SVG importieren oder Formen hinzufügen.");
}

QByteArray Document::state() const {
    QJsonArray arr;
    for (const auto &a : m_items)
        arr.append(QJsonObject{{"id", a.id},
                               {"name", a.name},
                               {"kind", a.kind},
                               {"text", a.text},
                               {"x", a.bounds.x()},
                               {"y", a.bounds.y()},
                               {"width", a.bounds.width()},
                               {"height", a.bounds.height()},
                               {"layer", a.layer},
                               {"visible", a.visible},
                               {"svg", QString::fromLatin1(a.svg.toBase64())}});
    return QJsonDocument(QJsonObject{{"format", "blazer-studio"},
                                     {"version", 1},
                                     {"selected", m_selected},
                                     {"title", m_title},
                                     {"objects", arr},
                                     {"layers", QJsonArray::fromVariantList(m_layers)}})
        .toJson();
}
bool Document::restore(const QByteArray &bytes) {
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(bytes, &error);
    auto root = doc.object();
    if (error.error != QJsonParseError::NoError || root["format"] != "blazer-studio" ||
        root["version"].toInt() != 1 || !root["objects"].isArray() ||
        root["objects"].toArray().size() > 2000)
        return false;
    auto layers = root["layers"].toArray().toVariantList();
    if (layers.size() != 2)
        return false;
    for (const auto &v : layers) {
        auto l = v.toMap();
        auto speed = l["speed"].toDouble(), power = l["power"].toDouble();
        int passes = l["passes"].toInt();
        if (!isFiniteNumber(speed) || speed <= 0 || speed > 100000 || !isFiniteNumber(power) ||
            power < 0 || power > 100 || passes < 1 || passes > 100)
            return false;
    }
    QList<Artwork> items;
    for (auto v : root["objects"].toArray()) {
        auto o = v.toObject();
        Artwork a{o["id"].toString(),
                  o["name"].toString(),
                  o["kind"].toString(),
                  o["text"].toString(),
                  QRectF(o["x"].toDouble(), o["y"].toDouble(), o["width"].toDouble(),
                         o["height"].toDouble()),
                  o["layer"].toInt(),
                  o["visible"].toBool(),
                  QByteArray::fromBase64(o["svg"].toString().toLatin1())};
        if (!QStringList{"rect", "ellipse", "text", "svg"}.contains(a.kind) || a.layer < 0 ||
            a.layer > 1 || !isFiniteNumber(a.bounds.x()) || !isFiniteNumber(a.bounds.y()) ||
            !isFiniteNumber(a.bounds.width()) || !isFiniteNumber(a.bounds.height()) ||
            a.bounds.width() <= 0 || a.bounds.height() <= 0 || a.bounds.width() > 2000 ||
            a.bounds.height() > 2000 || std::abs(a.bounds.x()) > 2000 ||
            std::abs(a.bounds.y()) > 2000)
            return false;
        if (a.kind == "svg") {
            if (a.svg.contains("<!ENTITY") || a.svg.contains("<!DOCTYPE"))
                return false;
            QXmlStreamReader xml(a.svg);
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement())
                    for (const auto &at : xml.attributes())
                        if (at.name() == "href" && !at.value().startsWith(u'#'))
                            return false;
            }
            if (xml.hasError())
                return false;
            QSvgRenderer svg(a.svg);
            if (!svg.isValid())
                return false;
        }
        items.append(a);
    }
    beginResetModel();
    m_items = items;
    m_layers = layers;
    m_title = root["title"].toString("Unbenannt");
    int selected = root["selected"].toInt(items.isEmpty() ? -1 : 0);
    m_selected = selected >= -1 && selected < items.size() ? selected : -1;
    endResetModel();
    emit changed();
    emit selectionChanged();
    return true;
}
void Document::checkpoint() {
    m_undo.append(state());
    if (m_undo.size() > 50)
        m_undo.removeFirst();
    m_redo.clear();
}
void Document::undo() {
    if (m_undo.isEmpty())
        return;
    m_redo.append(state());
    restore(m_undo.takeLast());
    m_dirty = true;
    emit changed();
}
void Document::redo() {
    if (m_redo.isEmpty())
        return;
    m_undo.append(state());
    restore(m_redo.takeLast());
    m_dirty = true;
    emit changed();
}
void Document::addShape(const QString &kind) {
    if (!QStringList{"rect", "ellipse", "text"}.contains(kind))
        return;
    checkpoint();
    m_items.append({id(),
                    kind == "text"   ? "Neuer Text"
                    : kind == "rect" ? "Rechteck"
                                     : "Ellipse",
                    kind,
                    kind == "text" ? "TEXT" : "",
                    QRectF(65, 55, 50, 30),
                    kind == "text" ? 0 : 1,
                    true,
                    {}});
    m_selected = m_items.size() - 1;
    refresh();
}
void Document::removeSelected() {
    if (m_selected < 0)
        return;
    checkpoint();
    m_items.removeAt(m_selected);
    m_selected = m_items.isEmpty() ? -1 : std::min(m_selected, int(m_items.size() - 1));
    refresh();
}
void Document::duplicateSelected() {
    if (m_selected < 0)
        return;
    checkpoint();
    auto a = m_items[m_selected];
    a.id = id();
    a.name += " · Kopie";
    a.bounds.translate(5, 5);
    m_items.append(a);
    m_selected = m_items.size() - 1;
    refresh();
}
void Document::toggleVisible(int row) {
    if (row < 0 || row >= m_items.size())
        return;
    checkpoint();
    m_items[row].visible = !m_items[row].visible;
    refresh();
}
void Document::setProperty(const QString &key, const QVariant &v) {
    if (m_selected < 0)
        return;
    auto &a = m_items[m_selected];
    auto b = a.bounds;
    if (key == "name" || key == "text") {
        if (v.toString().size() > 1000)
            return;
        checkpoint();
        if (key == "name")
            a.name = v.toString();
        else
            a.text = v.toString();
        refresh();
        return;
    }
    bool ok = false;
    double n = v.toDouble(&ok);
    if (!ok || !isFiniteNumber(n))
        return;
    if (key == "x" || key == "y") {
        if (std::abs(n) > 2000)
            return;
    } else if (key == "width" || key == "height") {
        if (n < .1 || n > 2000)
            return;
    } else
        return;
    checkpoint();
    if (key == "x")
        b.moveLeft(n);
    else if (key == "y")
        b.moveTop(n);
    else if (key == "width")
        b.setWidth(n);
    else
        b.setHeight(n);
    a.bounds = b;
    refresh();
}
void Document::setLayer(int layer) {
    if (m_selected < 0 || layer < 0 || layer > 1 || m_items[m_selected].layer == layer)
        return;
    checkpoint();
    m_items[m_selected].layer = layer;
    refresh();
}
void Document::setLayerSetting(int layer, const QString &key, const QVariant &v) {
    if (layer < 0 || layer > 1)
        return;
    auto map = m_layers[layer].toMap();
    if (!map.contains(key) || key == "name" || key == "mode")
        return;
    if (key != "enabled") {
        bool ok;
        double n = v.toDouble(&ok);
        if (!ok || !isFiniteNumber(n))
            return;
        if (key == "speed" && (n < 1 || n > 100000))
            return;
        if (key == "power" && (n < 0 || n > 100))
            return;
        if (key == "passes" && (n < 1 || n > 100 || std::floor(n) != n))
            return;
    }
    checkpoint();
    map[key] = v;
    m_layers[layer] = map;
    refresh();
}
void Document::beginMove() {
    m_beforeMove = state();
}
void Document::moveSelection(QPointF pos) {
    if (m_selected < 0)
        return;
    m_items[m_selected].bounds.moveTopLeft(pos);
    m_dirty = true;
    emit changed();
    emit selectionChanged();
}
void Document::finishMove() {
    if (!m_beforeMove.isEmpty() && m_beforeMove != state()) {
        m_undo.append(m_beforeMove);
        if (m_undo.size() > 50)
            m_undo.removeFirst();
        m_redo.clear();
        emit changed();
    }
    m_beforeMove.clear();
}
bool Document::importSvg(const QUrl &url) {
    QFile f(url.toLocalFile());
    if (!f.open(QIODevice::ReadOnly) || f.size() > 8 * 1024 * 1024) {
        notify("SVG konnte nicht gelesen werden (max. 8 MB).");
        return false;
    }
    auto bytes = f.readAll();
    if (bytes.contains("<!DOCTYPE") || bytes.contains("<!ENTITY")) {
        notify("SVG mit DTD/Entities wird im Entwurf nicht unterstützt.");
        return false;
    }
    QXmlStreamReader xml(bytes);
    QString w, h;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (xml.name() == "svg" && w.isEmpty()) {
                w = xml.attributes().value("width").toString();
                h = xml.attributes().value("height").toString();
            }
            for (const auto &a : xml.attributes())
                if (a.name() == "href" && !a.value().startsWith(u'#')) {
                    notify("SVG mit externen oder eingebetteten Verweisen wird nicht unterstützt.");
                    return false;
                }
        }
    }
    if (xml.hasError()) {
        notify("Ungültiges SVG.");
        return false;
    }
    QSvgRenderer svg(bytes);
    if (!svg.isValid()) {
        notify("SVG konnte nicht dargestellt werden.");
        return false;
    }
    double width = physicalLength(w, svg.viewBoxF().width() * 25.4 / 96),
           height = physicalLength(h, svg.viewBoxF().height() * 25.4 / 96);
    if (!isFiniteNumber(width) || !isFiniteNumber(height) || width <= 0 || height <= 0 ||
        width > 2000 || height > 2000) {
        notify("Ungültige SVG-Abmessungen.");
        return false;
    }
    checkpoint();
    m_items.append({id(), f.fileName().section('/', -1), "svg", "",
                    QRectF((200 - width) / 2, (150 - height) / 2, width, height), 0, true, bytes});
    m_selected = m_items.size() - 1;
    refresh();
    notify("SVG als Grafik importiert · noch keine Zerlegung in Laserpfade.");
    return true;
}
bool Document::save(const QUrl &url) {
    QSaveFile f(url.toLocalFile());
    if (!f.open(QIODevice::WriteOnly)) {
        notify("Speichern fehlgeschlagen.");
        return false;
    }
    auto bytes = state();
    if (f.write(bytes) != bytes.size() || !f.commit()) {
        notify("Speichern fehlgeschlagen.");
        return false;
    }
    m_dirty = false;
    emit changed();
    notify("Projekt gespeichert.");
    return true;
}
bool Document::open(const QUrl &url) {
    QFile f(url.toLocalFile());
    if (!f.open(QIODevice::ReadOnly) || f.size() > 16 * 1024 * 1024 || !restore(f.readAll())) {
        notify("Keine gültige Blazer-Studio-Projektdatei.");
        return false;
    }
    m_undo.clear();
    m_redo.clear();
    m_dirty = false;
    emit changed();
    notify("Projekt geöffnet.");
    return true;
}
QPainterPath Document::outline(const Artwork &a) {
    QPainterPath p;
    if (a.kind == "ellipse")
        p.addEllipse(a.bounds);
    else
        p.addRect(a.bounds);
    return p;
}
double Document::pathLength() const {
    double n = 0;
    for (const auto &a : m_items)
        if (a.visible && (a.kind == "rect" || a.kind == "ellipse") &&
            m_layers[a.layer].toMap()["enabled"].toBool())
            n += outline(a).length();
    return n;
}
