#include "document.h"
#include "geometry.h"
#include "materiallibrary.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace {
constexpr qint64 maxProjectBytes = 16 * 1024 * 1024;
constexpr qint64 maxEmbeddedBytes = 8 * 1024 * 1024;
QString id() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
bool number(const QJsonObject &o, const QString &key, double minimum, double maximum) {
    const auto v = o[key];
    return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= minimum &&
           v.toDouble() <= maximum;
}
bool text(const QJsonObject &o, const QString &key, int max = 1000) {
    return o[key].isString() && o[key].toString().size() <= max;
}
QVariantMap layer(QString name, QString mode, double speed, double power) {
    return {{"name", name}, {"mode", mode},    {"speed", speed}, {"power", power},
            {"passes", 1},  {"enabled", true}, {"interval", .1}};
}
} // namespace
Document::Document(QObject *p) : QAbstractListModel(p) {
    demo();
}
int Document::rowCount(const QModelIndex &p) const {
    return p.isValid() ? 0 : m_items.size();
}
QVariant Document::data(const QModelIndex &i, int role) const {
    if (!i.isValid() || i.model() != this || i.column() != 0 || i.row() < 0 ||
        i.row() >= m_items.size())
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
    case LayerRole:
        return a.layer;
    case GroupRole:
        return a.groupId;
    case GroupNameRole:
        return a.groupName;
    case GroupFirstRole:
        return !a.groupId.isEmpty() && (i.row() == 0 || m_items[i.row() - 1].groupId != a.groupId);
    default:
        return {};
    }
}
QHash<int, QByteArray> Document::roleNames() const {
    return {{NameRole, "artName"},           {KindRole, "artKind"},
            {ColorRole, "artColor"},         {VisibleRole, "artVisible"},
            {LayerRole, "artLayer"},         {GroupRole, "artGroup"},
            {GroupNameRole, "artGroupName"}, {GroupFirstRole, "artGroupFirst"}};
}
QColor Document::layerColor(int n) const {
    return n == 0 ? QColor("#cb713c") : QColor("#4c7bd9");
}
QVariantList Document::selectedRows() const {
    QVariantList rows;
    for (int row : m_selection)
        rows.append(row);
    return rows;
}
QRectF Document::selectionBounds() const {
    QRectF bounds;
    bool first = true;
    for (int row : m_selection) {
        bounds = first ? m_items[row].bounds : bounds.united(m_items[row].bounds);
        first = false;
    }
    return bounds;
}
QVariantMap Document::selection() const {
    if (m_selection.isEmpty())
        return {};
    const auto &a = m_items[m_selected];
    const auto bounds = selectionBounds();
    int layer = a.layer;
    bool visible = true;
    bool group = !a.groupId.isEmpty();
    int groupCount = 0;
    for (const auto &item : m_items)
        if (item.groupId == a.groupId)
            ++groupCount;
    for (int row : m_selection) {
        if (m_items[row].layer != layer)
            layer = -1;
        visible &= m_items[row].visible;
        group &= m_items[row].groupId == a.groupId;
    }
    group &= groupCount == m_selection.size();
    const bool multi = m_selection.size() > 1;
    return {{"name", group && multi ? a.groupName
                     : multi        ? QString("%1 Objekte").arg(m_selection.size())
                                    : a.name},
            {"kind", multi ? (group ? "group" : "selection") : a.kind},
            {"text", multi ? "" : a.text},
            {"x", bounds.x()},
            {"y", bounds.y()},
            {"width", bounds.width()},
            {"height", bounds.height()},
            {"layer", layer},
            {"visible", visible},
            {"count", m_selection.size()},
            {"grouped", group},
            {"groupId", group ? a.groupId : QString()}};
}
void Document::select(int row) {
    if (row < -1 || row >= m_items.size())
        row = -1;
    const QList<int> rows = row < 0 ? QList<int>{} : QList<int>{row};
    if (m_selection == rows)
        return;
    finishMove();
    m_selected = row;
    m_selection = rows;
    emit selectionChanged();
}
void Document::toggleSelection(int row) {
    if (row < 0 || row >= m_items.size())
        return;
    finishMove();
    if (m_selection.contains(row))
        m_selection.removeAll(row);
    else
        m_selection.append(row);
    std::sort(m_selection.begin(), m_selection.end());
    m_selected = m_selection.isEmpty() ? -1 : m_selection.first();
    emit selectionChanged();
}
void Document::selectGroup(int row) {
    if (row < 0 || row >= m_items.size())
        return;
    const auto group = m_items[row].groupId;
    if (group.isEmpty()) {
        select(row);
        return;
    }
    finishMove();
    m_selection.clear();
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i].groupId == group)
            m_selection.append(i);
    m_selected = m_selection.first();
    emit selectionChanged();
}
void Document::normalizeGroups() {
    QMap<QString, int> counts;
    for (const auto &a : m_items)
        if (!a.groupId.isEmpty())
            ++counts[a.groupId];
    for (auto &a : m_items)
        if (counts.value(a.groupId) < 2) {
            a.groupId.clear();
            a.groupName.clear();
        }
}
void Document::groupSelection() {
    if (m_selection.size() < 2)
        return;
    if (selection()["grouped"].toBool())
        return;
    checkpoint();
    beginResetModel();
    const QString group = id(), name = QString("Gruppe %1").arg(m_items.size());
    QList<Artwork> grouped, others;
    int first = m_selection.first();
    for (int i = 0; i < m_items.size(); ++i) {
        auto a = m_items[i];
        if (m_selection.contains(i)) {
            a.groupId = group;
            a.groupName = name;
            grouped.append(a);
        } else
            others.append(a);
    }
    first = std::min(first, int(others.size()));
    // Insert only at a group boundary so regrouping a subset cannot split an existing group.
    while (first > 0 && first < others.size() && !others[first].groupId.isEmpty() &&
           others[first - 1].groupId == others[first].groupId)
        --first;
    for (int i = 0; i < grouped.size(); ++i)
        others.insert(first + i, grouped[i]);
    m_items = others;
    normalizeGroups();
    m_selection.clear();
    for (int i = 0; i < grouped.size(); ++i)
        m_selection.append(first + i);
    m_selected = first;
    endResetModel();
    refresh();
}
void Document::ungroupSelection() {
    QSet<QString> groups;
    for (int row : m_selection)
        if (!m_items[row].groupId.isEmpty())
            groups.insert(m_items[row].groupId);
    if (groups.isEmpty())
        return;
    checkpoint();
    for (auto &a : m_items)
        if (groups.contains(a.groupId)) {
            a.groupId.clear();
            a.groupName.clear();
        }
    if (!m_items.isEmpty())
        emit dataChanged(index(0), index(m_items.size() - 1));
    refresh();
}
bool Document::transformSelection(const QRectF &target, bool history) {
    if (m_selection.isEmpty() || !std::isfinite(target.x()) || !std::isfinite(target.y()) ||
        !std::isfinite(target.width()) || !std::isfinite(target.height()) || target.width() <= 0 ||
        target.height() <= 0)
        return false;
    const auto old = selectionBounds();
    if (old == target)
        return false;
    QList<QRectF> result;
    for (int row : m_selection) {
        const auto &b = m_items[row].bounds;
        QRectF r(target.x() + (b.x() - old.x()) * target.width() / old.width(),
                 target.y() + (b.y() - old.y()) * target.height() / old.height(),
                 b.width() * target.width() / old.width(),
                 b.height() * target.height() / old.height());
        if (!Geometry::validBounds(r))
            return false;
        result.append(r);
    }
    if (history)
        checkpoint();
    for (int i = 0; i < m_selection.size(); ++i)
        m_items[m_selection[i]].bounds = result[i];
    refresh();
    return true;
}
void Document::scaleSelection(double percent) {
    if (!std::isfinite(percent) || percent <= 0 || m_selection.isEmpty())
        return;
    auto b = selectionBounds();
    b.setSize(b.size() * (percent / 100.));
    transformSelection(b);
}
void Document::notify(const QString &s) {
    m_message = s;
    emit messageChanged();
}
Document::Snapshot Document::snapshot() const {
    return {m_items, m_layers, m_title, m_selected, m_material, m_selection};
}
bool Document::sameContent(const Snapshot &a, const Snapshot &b) const {
    return a.items == b.items && a.layers == b.layers && a.title == b.title &&
           a.material == b.material;
}
void Document::refresh(int row) {
    m_dirty = !sameContent(snapshot(), m_saved);
    if (row >= 0 && row < m_items.size())
        emit dataChanged(index(row), index(row));
    emit changed();
    emit selectionChanged();
}
void Document::markClean() {
    m_saved = snapshot();
    m_dirty = false;
}
void Document::applySnapshot(const Snapshot &s) {
    beginResetModel();
    m_items = s.items;
    m_layers = s.layers;
    m_title = s.title;
    m_selected = s.selected;
    m_selection = s.selection;
    m_material = s.material;
    m_beforeMove.reset();
    endResetModel();
    refresh();
}
void Document::demo() {
    beginResetModel();
    m_title = "Werkstattschild";
    m_layers = {layer("01 · Gravieren", "Gravur", 3000, 20),
                layer("02 · Schneiden", "Schnitt", 600, 80)};
    m_material = MaterialLibrary::example();
    m_items = {
        {id(), "Schildkontur", "rect", "", QRectF(30, 35, 140, 80), 1, true, {}},
        {id(), "Bohrung links", "ellipse", "", QRectF(37, 104, 4, 4), 1, true, {}},
        {id(), "Bohrung rechts", "ellipse", "", QRectF(159, 104, 4, 4), 1, true, {}},
        {id(), "MOIN", "text", "MOIN", QRectF(51, 60, 98, 32), 0, true, {}},
        {id(), "Unterzeile", "text", "ZEIT FÜR GUTE IDEEN", QRectF(55, 48, 90, 7), 0, true, {}}};
    m_selected = 0;
    m_selection = {0};
    m_undo.clear();
    m_redo.clear();
    m_beforeMove.reset();
    markClean();
    endResetModel();
    refresh();
    notify("Beispielprojekt · Parameter sind Entwurfswerte, keine Materialempfehlung.");
}
void Document::newProject() {
    demo();
    beginResetModel();
    m_items.clear();
    m_title = "Unbenannt";
    m_selected = -1;
    m_selection.clear();
    markClean();
    endResetModel();
    refresh();
    notify("Neues Projekt · SVG importieren oder Formen hinzufügen.");
}
void Document::checkpoint() {
    finishMove();
    m_undo.append(snapshot());
    while (m_undo.size() > 50)
        m_undo.removeFirst();
    m_redo.clear();
}
void Document::undo() {
    finishMove();
    if (m_undo.isEmpty())
        return;
    m_redo.append(snapshot());
    applySnapshot(m_undo.takeLast());
}
void Document::redo() {
    finishMove();
    if (m_redo.isEmpty())
        return;
    m_undo.append(snapshot());
    applySnapshot(m_redo.takeLast());
}
bool Document::canAppend(const Artwork &a) {
    if (m_items.size() >= Geometry::objectLimit) {
        notify("Maximal 2000 Objekte pro Projekt.");
        return false;
    }
    qint64 size = a.asset ? a.asset->bytes.size() : 0;
    for (const auto &i : m_items)
        if (i.asset)
            size += i.asset->bytes.size();
    if (size > maxEmbeddedBytes) {
        notify("Eingebettete SVGs dürfen zusammen maximal 8 MB groß sein.");
        return false;
    }
    return true;
}
void Document::appendArtwork(const Artwork &a) {
    if (!canAppend(a))
        return;
    checkpoint();
    const int row = m_items.size();
    beginInsertRows({}, row, row);
    m_items.append(a);
    m_selected = row;
    m_selection = {row};
    endInsertRows();
    refresh();
}
void Document::addShape(const QString &kind) {
    if (!QStringList{"rect", "ellipse", "text"}.contains(kind))
        return;
    appendArtwork({id(),
                   kind == "text"   ? "Neuer Text"
                   : kind == "rect" ? "Rechteck"
                                    : "Ellipse",
                   kind,
                   kind == "text" ? "TEXT" : "",
                   QRectF(65, 55, 50, 30),
                   kind == "text" ? 0 : 1,
                   true,
                   {}});
}
void Document::removeSelected() {
    if (m_selection.isEmpty())
        return;
    checkpoint();
    beginResetModel();
    int next = m_selection.first();
    for (auto it = m_selection.crbegin(); it != m_selection.crend(); ++it)
        m_items.removeAt(*it);
    normalizeGroups();
    m_selected = m_items.isEmpty() ? -1 : std::min(next, int(m_items.size() - 1));
    m_selection = m_selected < 0 ? QList<int>{} : QList<int>{m_selected};
    endResetModel();
    refresh();
}
void Document::duplicateSelected() {
    if (m_selection.isEmpty() || m_items.size() + m_selection.size() > Geometry::objectLimit)
        return;
    QList<Artwork> copies;
    QMap<QString, QString> groups;
    qint64 bytes = 0;
    for (const auto &a : m_items)
        if (a.asset)
            bytes += a.asset->bytes.size();
    for (int row : m_selection) {
        auto a = m_items[row];
        a.id = id();
        a.name = (a.name + " · Kopie").left(1000);
        a.bounds.translate(5, 5);
        if (!Geometry::validBounds(a.bounds))
            return;
        if (a.asset)
            bytes += a.asset->bytes.size();
        if (!a.groupId.isEmpty()) {
            if (!groups.contains(a.groupId))
                groups[a.groupId] = id();
            a.groupId = groups[a.groupId];
            a.groupName = (a.groupName + " · Kopie").left(1000);
        }
        copies.append(a);
    }
    if (bytes > maxEmbeddedBytes)
        return;
    checkpoint();
    beginResetModel();
    int first = m_items.size();
    m_items.append(copies);
    normalizeGroups();
    m_selection.clear();
    for (int i = first; i < m_items.size(); ++i)
        m_selection.append(i);
    m_selected = first;
    endResetModel();
    refresh();
}
void Document::toggleVisible(int row) {
    if (row < 0 || row >= m_items.size())
        return;
    checkpoint();
    m_items[row].visible = !m_items[row].visible;
    refresh(row);
}
void Document::setProperty(const QString &key, const QVariant &value) {
    if (m_selected < 0)
        return;
    if (key == "name" || key == "text") {
        if (value.metaType().id() != QMetaType::QString || value.toString().size() > 1000)
            return;
        if (m_selection.size() > 1) {
            if (key != "name" || !selection()["grouped"].toBool() ||
                value.toString().trimmed().isEmpty())
                return;
            if (m_items[m_selected].groupName == value.toString())
                return;
            checkpoint();
            for (int row : m_selection)
                m_items[row].groupName = value.toString();
            emit dataChanged(index(0), index(m_items.size() - 1));
            refresh();
            return;
        }
        const auto &a = m_items[m_selected];
        if ((key == "name" ? a.name : a.text) == value.toString())
            return;
        checkpoint();
        if (key == "name")
            m_items[m_selected].name = value.toString();
        else
            m_items[m_selected].text = value.toString();
        refresh(m_selected);
        return;
    }
    bool ok;
    double n = value.toDouble(&ok);
    if (!ok || !std::isfinite(n) || value.metaType().id() == QMetaType::Bool)
        return;
    auto bounds = selectionBounds();
    if (key == "x")
        bounds.moveLeft(n);
    else if (key == "y")
        bounds.moveTop(n);
    else if (key == "width")
        bounds.setWidth(n);
    else if (key == "height")
        bounds.setHeight(n);
    else
        return;
    transformSelection(bounds);
}
void Document::setLayer(int layerIndex) {
    if (m_selection.isEmpty() || layerIndex < 0 || layerIndex > 1)
        return;
    bool change = false;
    for (int row : m_selection)
        change |= m_items[row].layer != layerIndex;
    if (!change)
        return;
    checkpoint();
    for (int row : m_selection)
        m_items[row].layer = layerIndex;
    emit dataChanged(index(0), index(m_items.size() - 1));
    refresh();
}
void Document::setLayerSetting(int layerIndex, const QString &key, const QVariant &value) {
    if (layerIndex < 0 || layerIndex > 1)
        return;
    auto map = m_layers[layerIndex].toMap();
    QVariant canonical;
    if (key == "enabled") {
        if (value.metaType().id() != QMetaType::Bool)
            return;
        canonical = value.toBool();
    } else {
        bool ok;
        double n = value.toDouble(&ok);
        if (!ok || !std::isfinite(n) || value.metaType().id() == QMetaType::Bool)
            return;
        if (key == "speed" && n >= 1 && n <= 100000)
            canonical = n;
        else if (key == "power" && n >= 0 && n <= 100)
            canonical = n;
        else if (key == "passes" && n >= 1 && n <= 100 && std::floor(n) == n)
            canonical = int(n);
        else if (key == "interval" && n >= .001 && n <= 10)
            canonical = n;
        else
            return;
    }
    if (map[key] == canonical)
        return;
    checkpoint();
    map[key] = canonical;
    m_layers[layerIndex] = map;
    refresh();
}
bool Document::applyMaterial(const QVariantMap &profile) {
    QString error;
    if (!MaterialLibrary::validate(profile, error)) {
        notify(error);
        return false;
    }
    auto updated = m_layers;
    for (int i = 0; i < 2; ++i) {
        auto values = profile[i == 0 ? "engrave" : "cut"].toMap();
        auto map = updated[i].toMap();
        for (const QString key : {"speed", "power", "passes", "enabled", "interval"})
            if (values.contains(key))
                map[key] = values[key];
        updated[i] = map;
    }
    if (m_material == profile && m_layers == updated)
        return true;
    checkpoint();
    m_layers = updated;
    m_material = profile;
    refresh();
    notify("Material übernommen: " + profile["name"].toString() +
           " · eigene Prüfung erforderlich.");
    return true;
}
bool Document::materialModified() const {
    if (m_material.isEmpty())
        return false;
    for (int i = 0; i < 2; ++i) {
        auto expected = m_material[i == 0 ? "engrave" : "cut"].toMap();
        auto actual = m_layers[i].toMap();
        for (const QString key : {"speed", "power", "passes", "enabled", "interval"})
            if (expected.contains(key) && expected[key] != actual[key])
                return true;
    }
    return false;
}
void Document::beginMove() {
    if (m_selected >= 0 && !m_beforeMove)
        m_beforeMove = snapshot();
}
void Document::moveSelection(QPointF pos) {
    if (m_selected < 0 || !m_beforeMove)
        return;
    auto bounds = selectionBounds();
    bounds.moveTopLeft(pos);
    transformSelection(bounds, false);
}
void Document::finishMove() {
    if (!m_beforeMove)
        return;
    if (!sameContent(*m_beforeMove, snapshot())) {
        m_undo.append(*m_beforeMove);
        while (m_undo.size() > 50)
            m_undo.removeFirst();
        m_redo.clear();
    }
    m_beforeMove.reset();
    emit changed();
}
bool Document::importSvg(const QUrl &url) {
    QFile file(url.toLocalFile());
    if (!url.isLocalFile() || !file.open(QIODevice::ReadOnly) || file.size() > maxEmbeddedBytes) {
        notify("SVG konnte nicht gelesen werden (max. 8 MB).");
        return false;
    }
    QString error;
    auto asset = SvgAsset::parse(file.read(maxEmbeddedBytes + 1), error);
    if (!asset) {
        notify(error);
        return false;
    }
    Artwork artwork{id(),
                    file.fileName().section('/', -1),
                    "svg",
                    "",
                    QRectF((Geometry::bedWidth - asset->sizeMm.width()) / 2,
                           (Geometry::bedHeight - asset->sizeMm.height()) / 2,
                           asset->sizeMm.width(), asset->sizeMm.height()),
                    0,
                    true,
                    asset};
    if (!canAppend(artwork))
        return false;
    appendArtwork(artwork);
    notify("SVG als Grafik importiert · noch keine Zerlegung in Laserpfade.");
    return true;
}
QByteArray Document::serialize() const {
    QJsonArray objects;
    for (const auto &a : m_items)
        objects.append(QJsonObject{
            {"id", a.id},
            {"name", a.name},
            {"kind", a.kind},
            {"text", a.text},
            {"x", a.bounds.x()},
            {"y", a.bounds.y()},
            {"width", a.bounds.width()},
            {"height", a.bounds.height()},
            {"layer", a.layer},
            {"visible", a.visible},
            {"groupId", a.groupId},
            {"groupName", a.groupName},
            {"svg", a.asset ? QString::fromLatin1(a.asset->bytes.toBase64()) : QString()}});
    return QJsonDocument(QJsonObject{{"format", "blazer-studio"},
                                     {"version", 3},
                                     {"units", "mm"},
                                     {"origin", "bottom-left"},
                                     {"title", m_title},
                                     {"selected", m_selected},
                                     {"selectedRows", QJsonArray::fromVariantList(selectedRows())},
                                     {"objects", objects},
                                     {"layers", QJsonArray::fromVariantList(m_layers)},
                                     {"material", QJsonObject::fromVariantMap(m_material)}})
        .toJson();
}
std::optional<Document::Snapshot> Document::parse(const QByteArray &bytes) const {
    if (bytes.size() > maxProjectBytes)
        return {};
    QJsonParseError error;
    auto json = QJsonDocument::fromJson(bytes, &error);
    auto root = json.object();
    if (error.error != QJsonParseError::NoError || root["format"] != "blazer-studio" ||
        !number(root, "version", 1, 3) ||
        std::floor(root["version"].toDouble()) != root["version"].toDouble() ||
        !text(root, "title") || !root["objects"].isArray() || !root["layers"].isArray())
        return {};
    const int version = root["version"].toInt();
    if (version >= 2 && (root["units"] != "mm" || root["origin"] != "bottom-left"))
        return {};
    auto objects = root["objects"].toArray();
    auto layers = root["layers"].toArray();
    if (objects.size() > Geometry::objectLimit || layers.size() != 2)
        return {};
    Snapshot state;
    state.title = root["title"].toString();
    for (int i = 0; i < layers.size(); ++i) {
        auto l = layers[i].toObject();
        if (!text(l, "name") || !text(l, "mode") ||
            l["mode"].toString() != (i == 0 ? "Gravur" : "Schnitt") || !l["enabled"].isBool() ||
            !number(l, "speed", 1, 100000) || !number(l, "power", 0, 100) ||
            !number(l, "passes", 1, 100) ||
            std::floor(l["passes"].toDouble()) != l["passes"].toDouble())
            return {};
        if (!l.contains("interval"))
            l["interval"] = .1;
        else if (!number(l, "interval", .001, 10))
            return {};
        auto map = l.toVariantMap();
        map["passes"] = l["passes"].toInt();
        state.layers.append(map);
    }
    QSet<QString> ids;
    qint64 embedded = 0;
    for (const auto &value : objects) {
        if (!value.isObject())
            return {};
        auto o = value.toObject();
        if (!text(o, "id", 50) || QUuid(o["id"].toString()).isNull() ||
            ids.contains(o["id"].toString()) || !text(o, "name") || !text(o, "text") ||
            !text(o, "kind", 20) || !o["visible"].isBool() || !o["svg"].isString() ||
            !number(o, "x", -2000, 2000) || !number(o, "y", -2000, 2000) ||
            !number(o, "width", .1, 2000) || !number(o, "height", .1, 2000) ||
            !number(o, "layer", 0, 1) || std::floor(o["layer"].toDouble()) != o["layer"].toDouble())
            return {};
        ids.insert(o["id"].toString());
        Artwork a{o["id"].toString(),
                  o["name"].toString(),
                  o["kind"].toString(),
                  o["text"].toString(),
                  QRectF(o["x"].toDouble(), o["y"].toDouble(), o["width"].toDouble(),
                         o["height"].toDouble()),
                  o["layer"].toInt(),
                  o["visible"].toBool(),
                  {}};
        if (!QStringList{"rect", "ellipse", "text", "svg"}.contains(a.kind))
            return {};
        if (version == 1)
            a.bounds.moveTop(Geometry::bedHeight - a.bounds.y() - a.bounds.height());
        if (!Geometry::validBounds(a.bounds))
            return {};
        const auto encoded = o["svg"].toString().toLatin1();
        if (a.kind == "svg") {
            auto decoded =
                QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded)
                return {};
            embedded += decoded.decoded.size();
            if (embedded > maxEmbeddedBytes)
                return {};
            QString svgError;
            a.asset = SvgAsset::parse(decoded.decoded, svgError);
            if (!a.asset)
                return {};
        } else if (!encoded.isEmpty())
            return {};
        if (version >= 3) {
            if (!text(o, "groupId", 50) || !text(o, "groupName"))
                return {};
            a.groupId = o["groupId"].toString();
            a.groupName = o["groupName"].toString();
            if ((!a.groupId.isEmpty() &&
                 (QUuid(a.groupId).isNull() || a.groupName.trimmed().isEmpty())) ||
                (a.groupId.isEmpty() && !a.groupName.isEmpty()))
                return {};
        }
        state.items.append(a);
    }
    if (root.contains("material")) {
        if (!root["material"].isObject())
            return {};
        state.material = root["material"].toObject().toVariantMap();
        QString error;
        if (!state.material.isEmpty() && !MaterialLibrary::validate(state.material, error))
            return {};
    }
    if (root.contains("selected") &&
        (!number(root, "selected", -1, Geometry::objectLimit) ||
         std::floor(root["selected"].toDouble()) != root["selected"].toDouble()))
        return {};
    state.selected = root["selected"].toInt(state.items.isEmpty() ? -1 : 0);
    if (state.selected >= state.items.size())
        return {};
    if (version >= 3) {
        if (!root["selectedRows"].isArray())
            return {};
        QSet<int> rows;
        for (const auto &v : root["selectedRows"].toArray()) {
            double n = v.toDouble(-1);
            if (!std::isfinite(n) || n < 0 || n >= state.items.size())
                return {};
            int row = int(n);
            if (!v.isDouble() || n != row || row < 0 || row >= state.items.size() ||
                rows.contains(row))
                return {};
            rows.insert(row);
            state.selection.append(row);
        }
        std::sort(state.selection.begin(), state.selection.end());
        if ((state.selection.isEmpty() && state.selected != -1) ||
            (!state.selection.isEmpty() && !rows.contains(state.selected)))
            return {};
        QMap<QString, int> counts;
        QMap<QString, QString> names;
        QSet<QString> closed;
        QString previous;
        for (const auto &a : state.items) {
            if (a.groupId != previous) {
                if (!previous.isEmpty())
                    closed.insert(previous);
                if (closed.contains(a.groupId))
                    return {};
                previous = a.groupId;
            }
            if (a.groupId.isEmpty())
                continue;
            if (names.contains(a.groupId) && names[a.groupId] != a.groupName)
                return {};
            names[a.groupId] = a.groupName;
            ++counts[a.groupId];
        }
        for (int n : counts)
            if (n < 2)
                return {};
    } else if (state.selected >= 0)
        state.selection = {state.selected};
    return state;
}
bool Document::save(const QUrl &url) {
    finishMove();
    if (!url.isLocalFile()) {
        notify("Bitte einen lokalen Dateipfad wählen.");
        return false;
    }
    auto bytes = serialize();
    if (bytes.size() > maxProjectBytes) {
        notify("Projekt ist größer als 16 MB; Speichern abgebrochen.");
        return false;
    }
    QSaveFile file(url.toLocalFile());
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        notify("Speichern fehlgeschlagen; vorhandene Datei bleibt erhalten.");
        return false;
    }
    markClean();
    emit changed();
    notify("Projekt gespeichert.");
    return true;
}
bool Document::open(const QUrl &url) {
    QFile file(url.toLocalFile());
    if (!url.isLocalFile() || !file.open(QIODevice::ReadOnly) || file.size() > maxProjectBytes) {
        notify("Projekt konnte nicht gelesen werden.");
        return false;
    }
    auto state = parse(file.read(maxProjectBytes + 1));
    if (!state) {
        notify("Keine gültige Blazer-Studio-Projektdatei; aktuelles Projekt bleibt erhalten.");
        return false;
    }
    applySnapshot(*state);
    m_undo.clear();
    m_redo.clear();
    markClean();
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
QVariantMap Document::activeBounds() const {
    QRectF bounds;
    int count = 0;
    for (const auto &a : m_items)
        if (a.visible && m_layers[a.layer].toMap()["enabled"].toBool()) {
            bounds = count ? bounds.united(a.bounds) : a.bounds;
            ++count;
        }
    return {{"valid", count > 0},
            {"x", bounds.x()},
            {"y", bounds.y()},
            {"width", bounds.width()},
            {"height", bounds.height()}};
}
QStringList Document::issues() const {
    QStringList result;
    int count = 0;
    for (const auto &a : m_items)
        if (a.visible && m_layers[a.layer].toMap()["enabled"].toBool()) {
            ++count;
            if (!Geometry::insideBed(a.bounds))
                result << a.name + ": außerhalb des Arbeitsraums";
        }
    if (!count)
        result << "Keine sichtbaren Objekte mit aktivierter Bearbeitung.";
    return result;
}
