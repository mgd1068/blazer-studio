#include "document.h"
#include "atomicfile.h"
#include "geometry.h"
#include "imagelimits.h"
#include "materiallibrary.h"
#include "vectorutil.h"
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <QXmlStreamWriter>
#include <algorithm>

namespace {
constexpr qint64 maxProjectBytes = ImageLimits::projectBytes;
constexpr qint64 maxEmbeddedBytes = ImageLimits::embeddedBytes;
constexpr qint64 maxReliefPixels = ImageLimits::documentPixels;
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
bool validMetadata(const QString &title, const QString &notes) {
    return !title.trimmed().isEmpty() && title.size() <= 1000 && !title.contains('\n') &&
           !title.contains('\r') && notes.size() <= 16000;
}
QVariantMap layer(QString name, QString mode, double speed, double power) {
    return {{"name", name}, {"mode", mode},    {"speed", speed}, {"power", power},
            {"passes", 1},  {"enabled", true}, {"interval", .1}};
}
QVariantList materialLayers(const QVariantList &current, const QVariantMap &profile) {
    auto updated = current;
    for (int i = 0; i < 2; ++i) {
        const auto values = profile[i == 0 ? "engrave" : "cut"].toMap();
        auto map = updated[i].toMap();
        for (const QString key : {"speed", "power", "passes", "enabled", "interval"})
            if (values.contains(key))
                map[key] = values[key];
        updated[i] = map;
    }
    return updated;
}
} // namespace
Document::Document(QObject *p) : QAbstractListModel(p) {
    connect(&m_preparation, &JobPreparation::changed, this, &Document::changed);
    connect(&m_batch, &BatchExport::changed, this, &Document::changed);
    connect(&m_batch, &BatchExport::finished, this, &Document::notify);
    connect(&m_imageExport, &ImageExport::changed, this, &Document::changed);
    connect(&m_imageExport, &ImageExport::finished, this, &Document::notify);
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
    case OnPageRole:
        return a.page == m_activePage;
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
            {GroupNameRole, "artGroupName"}, {GroupFirstRole, "artGroupFirst"},
            {OnPageRole, "artOnPage"}};
}
QColor Document::layerColor(int n) const {
    static const QList<QColor> colors{"#cb713c", "#4c7bd9", "#209b76",
                                      "#a85cb8", "#c39a29", "#548a9c"};
    return colors[std::max(0, n) % colors.size()];
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
            {"id", multi ? QString() : a.id},
            {"imageWidth", a.reliefAsset ? a.reliefAsset->image.width() : 0},
            {"imageHeight", a.reliefAsset ? a.reliefAsset->image.height() : 0},
            {"x", bounds.x()},
            {"y", bounds.y()},
            {"width", bounds.width()},
            {"height", bounds.height()},
            {"layer", layer},
            {"visible", visible},
            {"count", m_selection.size()},
            {"grouped", group},
            {"groupId", group ? a.groupId : QString()},
            {"customSettings", !a.settings.isEmpty()},
            {"relief", a.relief.toMap()},
            {"style", a.style},
            {"operations",
             [&a] {
                 QVariantList rows{a.layer};
                 for (int i : a.extraOperations)
                     rows << i;
                 return rows;
             }()},
            {"speed", a.settings.value("speed", m_layers[a.layer].toMap()["speed"])},
            {"power", a.settings.value("power", m_layers[a.layer].toMap()["power"])}};
}
void Document::select(int row) {
    if (row < -1 || row >= m_items.size())
        row = -1;
    if (row >= 0 && m_items[row].page != m_activePage)
        activatePage(m_items[row].page);
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
    checkpoint("Gruppieren");
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
    checkpoint("Gruppen auflösen");
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
        checkpoint("Position / Größe");
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
    return {m_items, m_layers, m_title, m_selected, m_material, m_selection, m_pages, m_activePage,
            m_notes};
}
bool Document::sameContent(const Snapshot &a, const Snapshot &b) const {
    return a.items == b.items && a.layers == b.layers && a.title == b.title &&
           a.material == b.material && a.pages == b.pages && a.notes == b.notes;
}
void Document::refresh(int row) {
    m_job.reset();
    if (m_background)
        m_preparation.submit(jobInput());
    m_dirty = !sameContent(snapshot(), m_saved);
    if (row >= 0 && row < m_items.size())
        emit dataChanged(index(row), index(row));
    emit changed();
    emit selectionChanged();
}
void Document::markClean() {
    m_saved = snapshot();
    m_dirty = false;
    ++m_historyRevision;
}
void Document::setFileUrl(const QUrl &url) {
    if (m_fileUrl == url)
        return;
    m_fileUrl = url;
    emit fileUrlChanged();
}
void Document::applySnapshot(const Snapshot &s) {
    beginResetModel();
    m_items = s.items;
    m_layers = s.layers;
    m_title = s.title;
    m_notes = s.notes;
    m_selected = s.selected;
    m_selection = s.selection;
    m_material = s.material;
    m_pages = s.pages;
    m_activePage = s.activePage;
    m_beforeMove.reset();
    endResetModel();
    refresh();
}
void Document::demo() {
    setFileUrl({});
    beginResetModel();
    m_title = "Werkstattschild";
    m_notes.clear();
    m_pages = {"Blatt 01"};
    m_activePage = 0;
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
    clearHistory();
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
void Document::clearHistory() {
    m_clipboardPasteHash.clear();
    m_clipboardPastePage=-1;
    m_clipboardPasteCount=0;
    m_undo.clear();
    m_redo.clear();
    ++m_historyRevision;
}
void Document::checkpoint(const QString &label) {
    finishMove();
    m_undo.append({snapshot(), label});
    while (m_undo.size() > 50)
        m_undo.removeFirst();
    m_redo.clear();
    ++m_historyRevision;
}
QVariantList Document::history() const {
    QVariantList result;
    const auto current = snapshot();
    auto append = [&](const Snapshot &state, const QString &label, int position) {
        result.append(QVariantMap{{"position", position}, {"label", label},
                                  {"current", position == m_undo.size()},
                                  {"saved", !m_fileUrl.isEmpty() && sameContent(state, m_saved)},
                                  {"objects", state.items.size()}, {"pages", state.pages.size()},
                                  {"activePage", state.activePage}});
    };
    const QString oldest = "Ältester verfügbarer Stand";
    for (int i = 0; i < m_undo.size(); ++i)
        append(m_undo[i].state, i ? m_undo[i - 1].label : oldest, i);
    append(current, m_undo.isEmpty() ? oldest : m_undo.last().label, m_undo.size());
    for (int i = m_redo.size() - 1; i >= 0; --i)
        append(m_redo[i].state, m_redo[i].label, result.size());
    return result;
}
bool Document::restoreHistory(int position, quint64 revision) {
    if (m_beforeMove || revision != m_historyRevision || position < 0 ||
        position > m_undo.size() + m_redo.size()) {
        notify("Der Verlauf hat sich geändert oder die Bearbeitung läuft noch. Bitte neu wählen.");
        return false;
    }
    if (position == m_undo.size())
        return true;
    auto current = snapshot();
    while (m_undo.size() > position) {
        auto previous = m_undo.takeLast();
        m_redo.append({std::move(current), previous.label});
        current = std::move(previous.state);
    }
    while (m_undo.size() < position) {
        auto next = m_redo.takeLast();
        m_undo.append({std::move(current), next.label});
        current = std::move(next.state);
    }
    ++m_historyRevision;
    applySnapshot(current); // One model reset and one new plan for the whole jump.
    return true;
}
bool Document::setMetadata(const QString &title, const QString &notes) {
    if (!validMetadata(title, notes)) {
        notify("Bitte einen einzeiligen Projektnamen mit 1–1000 Zeichen und höchstens 16000 "
               "Zeichen Notizen eingeben.");
        return false;
    }
    const auto name = title.trimmed();
    if (m_title == name && m_notes == notes)
        return true;
    checkpoint("Projektangaben");
    m_title = name;
    m_notes = notes;
    // Metadata is saved/recovered and undoable, but does not change toolpaths.
    m_dirty = !sameContent(snapshot(), m_saved);
    emit changed();
    notify("Projektangaben übernommen; zum dauerhaften Sichern das Projekt speichern.");
    return true;
}
void Document::undo() {
    finishMove();
    if (m_undo.isEmpty())
        return;
    restoreHistory(m_undo.size() - 1, m_historyRevision);
}
void Document::redo() {
    finishMove();
    if (m_redo.isEmpty())
        return;
    restoreHistory(m_undo.size() + 1, m_historyRevision);
}
bool Document::canAppend(const Artwork &a) {
    if (m_items.size() >= Geometry::objectLimit) {
        notify("Maximal 2000 Objekte pro Projekt.");
        return false;
    }
    qint64 size =
        (a.asset ? a.asset->bytes.size() : 0) + (a.reliefAsset ? a.reliefAsset->png.size() : 0);
    qint64 pixels =
        a.reliefAsset ? qint64(a.reliefAsset->image.width()) * a.reliefAsset->image.height() : 0;
    for (const auto &i : m_items) {
        if (i.reliefAsset)
            pixels += qint64(i.reliefAsset->image.width()) * i.reliefAsset->image.height();
        if (i.asset)
            size += i.asset->bytes.size();
        if (i.reliefAsset)
            size += i.reliefAsset->png.size();
    }
    if (pixels > maxReliefPixels) {
        notify("Tiefenkarten dürfen zusammen maximal 32 MiPixel enthalten.");
        return false;
    }
    if (size > maxEmbeddedBytes) {
        notify("Eingebettete Grafiken dürfen zusammen maximal 40 MiB groß sein.");
        return false;
    }
    return true;
}
void Document::appendArtwork(const Artwork &a) {
    if (!canAppend(a))
        return;
    static const QMap<QString, QString> labels{{"rect", "Rechteck hinzufügen"},
        {"ellipse", "Ellipse hinzufügen"}, {"text", "Text hinzufügen"},
        {"svg", "Vektor importieren"}, {"image", "Bild importieren"},
        {"relief", "Relief importieren"}, {"path", "Pfad hinzufügen"}};
    checkpoint(a.style.value("qr", false).toBool() ? QString("QR-Code hinzufügen")
                                                   : labels.value(a.kind, "Objekt hinzufügen"));
    const int row = m_items.size();
    beginInsertRows({}, row, row);
    auto item = a;
    item.page = m_activePage;
    m_items.append(item);
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
    removeSelection("Objekte löschen");
}
void Document::removeSelection(const QString &label) {
    if (m_selection.isEmpty())
        return;
    checkpoint(label);
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
    qint64 bytes = 0, pixels = 0;
    for (const auto &a : m_items) {
        if (a.asset)
            bytes += a.asset->bytes.size();
        if (a.reliefAsset) {
            bytes += a.reliefAsset->png.size();
            pixels += qint64(a.reliefAsset->image.width()) * a.reliefAsset->image.height();
        }
    }
    for (int row : m_selection) {
        auto a = m_items[row];
        a.id = id();
        a.name = (a.name + " · Kopie").left(1000);
        a.bounds.translate(5, 5);
        if (!Geometry::validBounds(a.bounds))
            return;
        if (a.asset)
            bytes += a.asset->bytes.size();
        if (a.reliefAsset) {
            bytes += a.reliefAsset->png.size();
            pixels += qint64(a.reliefAsset->image.width()) * a.reliefAsset->image.height();
        }
        if (!a.groupId.isEmpty()) {
            if (!groups.contains(a.groupId))
                groups[a.groupId] = id();
            a.groupId = groups[a.groupId];
            a.groupName = (a.groupName + " · Kopie").left(1000);
        }
        copies.append(a);
    }
    if (bytes > maxEmbeddedBytes || pixels > maxReliefPixels)
        return;
    checkpoint("Objekte kopieren");
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
    checkpoint("Sichtbarkeit");
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
            checkpoint("Gruppenname");
            for (int row : m_selection)
                m_items[row].groupName = value.toString();
            emit dataChanged(index(0), index(m_items.size() - 1));
            refresh();
            return;
        }
        const auto &a = m_items[m_selected];
        if ((key == "name" ? a.name : a.text) == value.toString())
            return;
        checkpoint(key == "name" ? "Objektname" : "Text");
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
    if (m_selection.isEmpty() || layerIndex < 0 || layerIndex >= m_layers.size())
        return;
    for (int row : m_selection)
        if ((m_items[row].kind == "relief" || m_items[row].kind == "image") &&
            m_layers[layerIndex].toMap()["mode"] == "Schnitt") {
            notify("Reliefbilder werden ausschließlich graviert.");
            return;
        }
    bool change = false;
    for (int row : m_selection)
        change |= m_items[row].layer != layerIndex;
    if (!change)
        return;
    checkpoint("Bearbeitung zuweisen");
    for (int row : m_selection)
        m_items[row].layer = layerIndex;
    emit dataChanged(index(0), index(m_items.size() - 1));
    refresh();
}
void Document::setLayerSetting(int layerIndex, const QString &key, const QVariant &value) {
    if (layerIndex < 0 || layerIndex >= m_layers.size())
        return;
    auto map = m_layers[layerIndex].toMap();
    QVariant canonical;
    if (key == "name" || key == "mode") {
        if (value.metaType().id() != QMetaType::QString || value.toString().isEmpty() ||
            value.toString().size() > 200 ||
            (key == "mode" && !QStringList{"Gravur", "Schnitt"}.contains(value.toString())))
            return;
        if (key == "mode" && value == "Schnitt")
            for (const auto &a : m_items)
                if (a.kind == "relief" &&
                    (a.layer == layerIndex || a.extraOperations.contains(layerIndex)))
                    return;
        canonical = value;
    } else if (key == "crosshatch" || key == "optimize" || key == "enabled") {
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
        else if ((key == "angle" || key == "angleIncrement") && n >= -360 && n <= 360)
            canonical = n;
        else if ((key == "kerf" || key == "scanOffset") && n >= -5 && n <= 5)
            canonical = n;
        else if ((key == "tabs" || key == "startPoint") && n >= 0 && n <= 100 && std::floor(n) == n)
            canonical = int(n);
        else if ((key == "tabWidth" || key == "leadIn" || key == "leadOut" || key == "overscan") &&
                 n >= 0 && n <= 20)
            canonical = n;
        else if (key == "acceleration" && n >= 1 && n <= 10000)
            canonical = n;
        else if (key == "interval" && n >= .001 && n <= 10)
            canonical = n;
        else
            return;
    }
    if (map[key] == canonical)
        return;
    checkpoint("Operationswerte");
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
    auto updated = materialLayers(m_layers, profile);
    if (m_material == profile && m_layers == updated)
        return true;
    checkpoint("Materialprofil");
    m_layers = updated;
    m_material = profile;
    for (auto &item : m_items)
        item.relief.depthPerPass = 0;
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
    for (const auto &a : m_items) {
        auto expected = m_material[a.layer == 0 ? "engrave" : "cut"].toMap();
        for (auto it = a.settings.begin(); it != a.settings.end(); ++it)
            if (expected.contains(it.key()) && expected[it.key()] != it.value())
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
        m_undo.append({*m_beforeMove, "Ziehen"});
        while (m_undo.size() > 50)
            m_undo.removeFirst();
        m_redo.clear();
        ++m_historyRevision;
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
    return serialize(snapshot());
}
QByteArray Document::serialize(const Snapshot &state) const {
    QJsonArray objects;
    for (const auto &a : state.items)
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
            {"page", a.page},
            {"path", VectorUtil::encode(a.path)},
            {"pathFillRule", int(a.path.fillRule())},
            {"style", QJsonObject::fromVariantMap(a.style)},
            {"extraOperations",
             [&a] {
                 QJsonArray r;
                 for (int i : a.extraOperations)
                     r.append(i);
                 return r;
             }()},
            {"groupId", a.groupId},
            {"groupName", a.groupName},
            {"settings", QJsonObject::fromVariantMap(a.settings)},
            {"relief",
             a.reliefAsset ? QJsonObject::fromVariantMap(a.relief.toMap()) : QJsonObject()},
            {"depthMap",
             a.reliefAsset ? QString::fromLatin1(a.reliefAsset->png.toBase64()) : QString()},
            {"svg", a.asset ? QString::fromLatin1(a.asset->bytes.toBase64()) : QString()}});
    return QJsonDocument(QJsonObject{{"format", "blazer-studio"},
                                     {"version", 8},
                                     {"units", "mm"},
                                     {"origin", "bottom-left"},
                                     {"pages", QJsonArray::fromStringList(state.pages)},
                                     {"activePage", state.activePage},
                                     {"title", state.title},
                                     {"notes", state.notes},
                                     {"selected", state.selected},
                                     {"selectedRows", [&state] {QJsonArray rows;for(int row:state.selection)rows.append(row);return rows;}()},
                                     {"objects", objects},
                                     {"layers", QJsonArray::fromVariantList(state.layers)},
                                     {"material", QJsonObject::fromVariantMap(state.material)}})
        .toJson();
}
std::optional<Document::Snapshot> Document::parse(const QByteArray &bytes) const {
    if (bytes.size() > maxProjectBytes)
        return {};
    QJsonParseError error;
    auto json = QJsonDocument::fromJson(bytes, &error);
    auto root = json.object();
    if (error.error != QJsonParseError::NoError || root["format"] != "blazer-studio" ||
        !number(root, "version", 1, 8) ||
        std::floor(root["version"].toDouble()) != root["version"].toDouble() ||
        !text(root, "title") || !root["objects"].isArray() || !root["layers"].isArray())
        return {};
    const int version = root["version"].toInt();
    if (version >= 2 && (root["units"] != "mm" || root["origin"] != "bottom-left"))
        return {};
    auto objects = root["objects"].toArray();
    auto layers = root["layers"].toArray();
    if (objects.size() > Geometry::objectLimit || layers.size() < 2 || layers.size() > 64)
        return {};
    Snapshot state;
    state.title = root["title"].toString();
    if (version >= 8) {
        if (!text(root, "notes", 16000))
            return {};
        state.notes = root["notes"].toString();
    }
    state.pages = {"Blatt 01"};
    if (version >= 6) {
        if (!root["pages"].isArray() || !number(root, "activePage", 0, 99) ||
            std::floor(root["activePage"].toDouble()) != root["activePage"].toDouble())
            return {};
        state.pages.clear();
        for (auto name : root["pages"].toArray()) {
            if (!name.isString() || name.toString().trimmed().isEmpty() ||
                name.toString().size() > 200)
                return {};
            state.pages.append(name.toString());
        }
        state.activePage = root["activePage"].toInt();
        if (state.pages.isEmpty() || state.pages.size() > 100 ||
            state.activePage >= state.pages.size())
            return {};
    }
    for (int i = 0; i < layers.size(); ++i) {
        auto l = layers[i].toObject();
        if (!text(l, "name") || !text(l, "mode") ||
            !QStringList{"Gravur", "Schnitt"}.contains(l["mode"].toString()) ||
            !l["enabled"].isBool() || !number(l, "speed", 1, 100000) ||
            !number(l, "power", 0, 100) || !number(l, "passes", 1, 100) ||
            std::floor(l["passes"].toDouble()) != l["passes"].toDouble())
            return {};
        if (!l.contains("interval"))
            l["interval"] = .1;
        else if (!number(l, "interval", .001, 10))
            return {};
        for (const QString key :
             {"angle", "angleIncrement", "kerf", "scanOffset", "tabs", "startPoint", "tabWidth",
              "leadIn", "leadOut", "overscan", "acceleration"})
            if (l.contains(key)) {
                double lo = 0, hi = 20;
                if (key == "angle" || key == "angleIncrement") {
                    lo = -360;
                    hi = 360;
                }
                if (key == "kerf" || key == "scanOffset") {
                    lo = -5;
                    hi = 5;
                }
                if (key == "tabs" || key == "startPoint")
                    hi = 100;
                if (key == "acceleration") {
                    lo = 1;
                    hi = 10000;
                }
                if (!number(l, key, lo, hi))
                    return {};
                if ((key == "tabs" || key == "startPoint") &&
                    std::floor(l[key].toDouble()) != l[key].toDouble())
                    return {};
            }
        for (const QString key : {"crosshatch", "optimize"})
            if (l.contains(key) && !l[key].isBool())
                return {};
        auto map = l.toVariantMap();
        map["passes"] = l["passes"].toInt();
        state.layers.append(map);
    }
    QSet<QString> ids;
    qint64 embedded = 0, pixels = 0;
    for (const auto &value : objects) {
        if (!value.isObject())
            return {};
        auto o = value.toObject();
        if (!text(o, "id", 50) || QUuid(o["id"].toString()).isNull() ||
            ids.contains(o["id"].toString()) || !text(o, "name") || !text(o, "text") ||
            !text(o, "kind", 20) || !o["visible"].isBool() || !o["svg"].isString() ||
            !number(o, "x", -2000, 2000) || !number(o, "y", -2000, 2000) ||
            !number(o, "width", .1, 2000) || !number(o, "height", .1, 2000) ||
            !number(o, "layer", 0, layers.size() - 1) ||
            std::floor(o["layer"].toDouble()) != o["layer"].toDouble())
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
        if (!QStringList{"rect", "ellipse", "text", "svg", "relief", "path", "image"}.contains(
                a.kind))
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
        if (version >= 4 && o.contains("settings")) {
            if (!o["settings"].isObject())
                return {};
            auto settings = o["settings"].toObject();
            for (auto it = settings.begin(); it != settings.end(); ++it) {
                if (it.key() == "speed") {
                    if (!number(settings, it.key(), 1, 100000))
                        return {};
                } else if (it.key() == "power") {
                    if (!number(settings, it.key(), 0, 100))
                        return {};
                } else
                    return {};
            }
            a.settings = settings.toVariantMap();
        }
        if (a.kind == "relief" || a.kind == "image") {
            if (version < 5 || layers[a.layer].toObject()["mode"] == "Schnitt" ||
                !o["depthMap"].isString() || !o["relief"].isObject() ||
                !ReliefSettings::fromMap(o["relief"].toObject().toVariantMap(), a.relief))
                return {};
            auto decoded = QByteArray::fromBase64Encoding(o["depthMap"].toString().toLatin1(),
                                                          QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded)
                return {};
            embedded += decoded.decoded.size();
            if (embedded > maxEmbeddedBytes)
                return {};
            a.reliefAsset = ReliefAsset::parse(decoded.decoded);
            if (!a.reliefAsset)
                return {};
            pixels += qint64(a.reliefAsset->image.width()) * a.reliefAsset->image.height();
            if (pixels > maxReliefPixels)
                return {};
        } else if (version >= 5 &&
                   ((!o["depthMap"].isUndefined() && o["depthMap"].toString() != "") ||
                    (!o["relief"].isUndefined() &&
                     (!o["relief"].isObject() || !o["relief"].toObject().isEmpty()))))
            return {};
        if (version >= 6) {
            if (!number(o, "page", 0, state.pages.size() - 1) ||
                std::floor(o["page"].toDouble()) != o["page"].toDouble() || !o["path"].isArray() ||
                !VectorUtil::decode(o["path"].toArray(), a.path) || !o["style"].isObject() ||
                !o["extraOperations"].isArray() || !number(o, "pathFillRule", 0, 1))
                return {};
            a.path.setFillRule(Qt::FillRule(o["pathFillRule"].toInt()));
            a.page = o["page"].toInt();
            a.style = o["style"].toObject().toVariantMap();
            if (a.style.size() > 20)
                return {};
            const QSet<QString> strings{"font", "imageMode"},
                booleans{"bold", "invert", "ellipseMask", "qr"};
            for (auto it = a.style.begin(); it != a.style.end(); ++it) {
                if (strings.contains(it.key())) {
                    if (it.value().metaType().id() != QMetaType::QString ||
                        it.value().toString().size() > 200)
                        return {};
                } else if (booleans.contains(it.key())) {
                    if (it.value().metaType().id() != QMetaType::Bool)
                        return {};
                } else {
                    auto type = it.value().metaType().id();
                    if (type != QMetaType::Double && type != QMetaType::Int &&
                        type != QMetaType::LongLong)
                        return {};
                    double n = it.value().toDouble();
                    if (!std::isfinite(n))
                        return {};
                    if (it.key() == "spacing") {
                        if (std::abs(n) > 100)
                            return {};
                    } else if (it.key() == "brightness") {
                        if (n < -1 || n > 1)
                            return {};
                    } else if ((it.key() == "contrast" || it.key() == "sharpness")) {
                        if (n < 0 || n > 3)
                            return {};
                    } else if (it.key() == "gamma") {
                        if (n < .1 || n > 5)
                            return {};
                    } else if (it.key() == "threshold") {
                        if (n < 0 || n > 255)
                            return {};
                    } else if (QStringList{"cropX", "cropY", "cropW", "cropH"}.contains(it.key())) {
                        if (n < 0 || n > ImageLimits::side || std::floor(n) != n)
                            return {};
                    } else
                        return {};
                }
            }
            for (auto op : o["extraOperations"].toArray()) {
                if (!op.isDouble() || std::floor(op.toDouble()) != op.toDouble() ||
                    op.toInt() < 0 || op.toInt() >= layers.size() || op.toInt() == a.layer ||
                    a.extraOperations.contains(op.toInt()) ||
                    (a.kind == "relief" && layers[op.toInt()].toObject()["mode"] == "Schnitt"))
                    return {};
                a.extraOperations.append(op.toInt());
            }
        }
        if (a.kind == "path" && (version < 6 || a.path.isEmpty()))
            return {};
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
    const QFileInfo target(url.toLocalFile());
    const auto isTemplate = [](const QFileInfo &file) {
        return file.suffix().compare("blazertemplate", Qt::CaseInsensitive) == 0;
    };
    if (isTemplate(target) || isTemplate(QFileInfo(target.canonicalFilePath()))) {
        notify("Bitte das neue Projekt als .blazer speichern. Eine Vorlage wird nur über "
               "Als Vorlage speichern geändert.");
        return false;
    }
    if (!url.isLocalFile()) {
        notify("Bitte einen lokalen Dateipfad wählen.");
        return false;
    }
    auto bytes = serialize();
    if (bytes.size() > maxProjectBytes) {
        notify("Projekt ist größer als 64 MiB; Speichern abgebrochen.");
        return false;
    }
    QString error;
    if (!AtomicFile::write(url.toLocalFile(), bytes, error, ".bak")) {
        notify(error);
        return false;
    }
    markClean();
    setFileUrl(QUrl::fromLocalFile(QFileInfo(url.toLocalFile()).absoluteFilePath()));
    emit changed();
    notify("Projekt gespeichert.");
    emit projectFileUsed(m_fileUrl);
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
    clearHistory();
    applySnapshot(*state);
    markClean();
    setFileUrl(QUrl::fromLocalFile(QFileInfo(url.toLocalFile()).absoluteFilePath()));
    emit changed();
    notify("Projekt geöffnet.");
    emit projectFileUsed(m_fileUrl);
    return true;
}
bool Document::saveTemplate(const QUrl &url) {
    if (!url.isLocalFile() ||
        QFileInfo(url.toLocalFile()).suffix().compare("blazertemplate", Qt::CaseInsensitive) != 0) {
        notify("Bitte eine lokale .blazertemplate-Datei für die Vorlage wählen.");
        return false;
    }
    finishMove();
    auto root = QJsonDocument::fromJson(serialize()).object();
    root["format"] = "blazer-studio-template";
    root["templateVersion"] = 1;
    const auto bytes = QJsonDocument(root).toJson();
    if (bytes.size() > maxProjectBytes) {
        notify("Vorlage ist größer als 64 MiB; Speichern abgebrochen.");
        return false;
    }
    QString error;
    if (!AtomicFile::write(url.toLocalFile(), bytes, error, ".bak")) {
        notify(error);
        return false;
    }
    notify(m_dirty ? "Vorlage gespeichert; Projektänderungen sind weiterhin ungespeichert."
                   : "Vorlage gespeichert.");
    return true;
}
bool Document::newFromTemplate(const QUrl &url) {
    QFile file(url.toLocalFile());
    if (!url.isLocalFile() || !file.open(QIODevice::ReadOnly) || file.size() > maxProjectBytes) {
        notify("Vorlage konnte nicht gelesen werden.");
        return false;
    }
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(file.read(maxProjectBytes + 1), &error);
    auto root = json.object();
    if (error.error != QJsonParseError::NoError || root["format"] != "blazer-studio-template" ||
        root["templateVersion"] != 1) {
        notify("Keine unterstützte Blazer-Vorlage; das aktuelle Projekt bleibt erhalten.");
        return false;
    }
    root["format"] = "blazer-studio";
    root.remove("templateVersion");
    const auto state = parse(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!state) {
        notify("Vorlage enthält kein gültiges Projekt; das aktuelle Projekt bleibt erhalten.");
        return false;
    }
    // A template starts an independent, unsaved document with fresh history.
    // Its source is never treated as the project's ordinary save destination.
    clearHistory();
    m_saved = Snapshot{};
    setFileUrl({});
    applySnapshot(*state);
    notify("Neues Projekt aus Vorlage; bitte unter eigenem Projektnamen speichern.");
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
        if (outputs(a) && (a.kind == "rect" || a.kind == "ellipse"))
            n += outline(a).length();
    return n;
}
QVariantMap Document::activeBounds() const {
    QRectF bounds;
    int count = 0;
    for (const auto &a : m_items)
        if (outputs(a)) {
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
        if (outputs(a)) {
            ++count;
            if (!Geometry::insideBed(a.bounds))
                result << a.name + ": außerhalb des Arbeitsraums";
        }
    if (!count)
        result << "Keine sichtbaren Objekte mit aktivierter Bearbeitung.";
    return result;
}

const JobPlan &Document::job() const {
    if (m_background)
        return m_preparation.plan();
    if (!m_job)
        m_job = JobPlanner::build(*this);
    return *m_job;
}
bool Document::exportGcode(const QUrl &url) {
    if (!url.isLocalFile() || !job().valid()) {
        notify(job().errors.join("\n"));
        return false;
    }
    const auto bytes = (job().gcode().join("\n") + "\n").toUtf8();
    QSaveFile f(url.toLocalFile());
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        notify("GCode konnte nicht gespeichert werden.");
        return false;
    }
    notify("GRBL-GCode exportiert · G21/G90, M4, S-Max 1000.");
    return true;
}
bool Document::importImage(const QUrl &url) {
    QImageReader reader(url.toLocalFile());
    reader.setAutoTransform(true);
    auto size = reader.size();
    if (!url.isLocalFile() || !size.isValid() || size.width() > ImageLimits::side ||
        size.height() > ImageLimits::side ||
        qint64(size.width()) * size.height() > ImageLimits::pixels) {
        notify("Bild ungültig oder größer als 4096 Pixel je Seite oder 16 MiPixel.");
        return false;
    }
    auto image = reader.read();

    auto asset = ReliefAsset::fromImage(image);
    if (!asset) {
        notify("Bild konnte nicht gelesen werden oder benötigt als PNG mehr als 40 MiB.");
        return false;
    }
    Artwork a;
    a.id = id();
    a.name = QFileInfo(url.toLocalFile()).completeBaseName();
    a.kind = "image";
    double mmPerPixel = std::min(.1, 100. / std::max(image.width(), image.height()));
    a.bounds = QRectF(10, 10, image.width() * mmPerPixel, image.height() * mmPerPixel);
    a.layer = 0;
    a.reliefAsset = asset;
    a.settings = {{"speed", m_layers[0].toMap()["speed"]}, {"power", m_layers[0].toMap()["power"]}};
    a.style = {{"imageMode", "dither"}, {"brightness", 0.}, {"contrast", 1.}, {"gamma", 1.}};
    if (!canAppend(a))
        return false;
    appendArtwork(a);
    notify("Bild importiert; Bildmodus und Korrekturen unter Werkzeuge.");
    return true;
}
void Document::setObjectSetting(const QString &key, double value) {
    if (m_selection.isEmpty() || !std::isfinite(value) || (key != "speed" && key != "power") ||
        (key == "speed" && (value < 1 || value > 100000)) ||
        (key == "power" && (value < 0 || value > 100)))
        return;
    bool changed = false;
    for (int row : m_selection)
        changed |= m_items[row].settings.value(key) != value;
    if (!changed)
        return;
    checkpoint("Objektwerte");
    for (int row : m_selection) {
        if (m_items[row].settings.value(key) != value)
            m_items[row].relief.depthPerPass = 0;
        m_items[row].settings[key] = value;
    }
    refresh();
}
bool Document::createMaterialTest(double minSpeed, double maxSpeed, double minPower,
                                  double maxPower, int steps, double moduleW) {
    const auto plan = JobPlanner::materialTest(minSpeed, maxSpeed, minPower, maxPower, steps);
    if (!plan.valid() || !std::isfinite(moduleW) || moduleW < .1 || moduleW > 100) {
        notify("Ungültige Materialtest-Parameter.");
        return false;
    }
    newProject();
    checkpoint("Materialtest");
    beginResetModel();
    m_title = "Materialtest";
    m_material = MaterialLibrary::example();
    m_material["id"] = id();
    m_material["name"] = "Materialtest";
    m_material["moduleW"] = moduleW;
    m_material["source"] = "Eigener Materialtest · Werte nicht geprüft";
    m_layers[0] = layer("01 · Gravieren", "Gravur", minSpeed, minPower);
    auto cut = m_layers[1].toMap();
    cut["enabled"] = false;
    m_layers[1] = cut;
    m_material["engrave"] = m_layers[0];
    m_material["cut"] = m_layers[1];
    for (int row = 0; row < steps; ++row)
        for (int col = 0; col < steps; ++col) {
            double speed = minSpeed + (maxSpeed - minSpeed) * row / (steps - 1),
                   power = minPower + (maxPower - minPower) * col / (steps - 1);
            Artwork a{id(),
                      QString("Z%1 S%2 · %3 mm/min · %4 %")
                          .arg(row + 1)
                          .arg(col + 1)
                          .arg(speed)
                          .arg(power),
                      "rect",
                      "",
                      QRectF(10 + col * 8, 10 + row * 8, 5, 5),
                      0,
                      true,
                      {}};
            a.settings = {{"speed", speed}, {"power", power}};
            auto item = a;
            item.page = m_activePage;
            m_items.append(item);
        }
    m_selected = 0;
    m_selection = {0};
    endResetModel();
    refresh();
    notify("Materialtest: Zeilen unten→oben = Geschwindigkeit, Spalten links→rechts = Leistung. "
           "Noch kein Laserstart.");
    return true;
}

bool Document::importRelief(const QUrl &url) {
    QImageReader reader(url.toLocalFile());
    reader.setAutoTransform(true);
    auto size = reader.size();
    if (!url.isLocalFile() || !size.isValid() || size.width() > ImageLimits::side ||
        size.height() > ImageLimits::side ||
        qint64(size.width()) * size.height() > ImageLimits::pixels) {
        notify("Tiefenkarte ungültig oder größer als 4096 Pixel je Seite oder 16 MiPixel.");
        return false;
    }
    auto image = reader.read();

    auto asset = ReliefAsset::fromImage(image);
    if (!asset) {
        notify("Tiefenkarte konnte nicht gelesen werden.");
        return false;
    }
    Artwork a{id(),
              QFileInfo(url.toLocalFile()).completeBaseName(),
              "relief",
              "",
              QRectF(10, 10,
                     image.width() * std::min(.1, 100. / std::max(image.width(), image.height())),
                     image.height() * std::min(.1, 100. / std::max(image.width(), image.height()))),
              0,
              true,
              {}};
    a.reliefAsset = asset;
    // Keep initial physical size editable, including tiny input maps.
    a.settings = {{"speed", m_layers[0].toMap()["speed"]}, {"power", m_layers[0].toMap()["power"]}};
    if (!canAppend(a))
        return false;
    appendArtwork(a);
    notify("Tiefenkarte importiert · Weiß bleibt stehen, Schwarz erhält alle Schichten.");
    return true;
}
void Document::setReliefSetting(const QString &key, const QVariant &value) {
    if (m_selection.size() != 1 || m_items[m_selected].kind != "relief")
        return;
    auto map = m_items[m_selected].relief.toMap();
    if (!map.contains(key))
        return;
    map[key] = value;
    ReliefSettings result;
    if (!ReliefSettings::fromMap(map, result)) {
        notify(
            "Reliefwerte ungültig: 1–256 Schichten, Raster 0,02–2 mm, Gamma 0,1–5, Abtrag 0–1 mm.");
        emit selectionChanged();
        return;
    }
    if (result == m_items[m_selected].relief)
        return;
    if (result.interval != m_items[m_selected].relief.interval ||
        result.crosshatch != m_items[m_selected].relief.crosshatch ||
        result.cleanupEvery != m_items[m_selected].relief.cleanupEvery ||
        result.cleanupPower != m_items[m_selected].relief.cleanupPower)
        result.depthPerPass = 0;
    checkpoint("Reliefwerte");
    m_items[m_selected].relief = result;
    refresh();
}

bool Document::exportMaterialTest(const QUrl &url, double minSpeed, double maxSpeed,
                                  double minPower, double maxPower, int steps) {
    auto plan = JobPlanner::materialTest(minSpeed, maxSpeed, minPower, maxPower, steps);
    if (!url.isLocalFile() || !plan.valid()) {
        notify(plan.errors.join("; "));
        return false;
    }
    QString guide =
        QString("(Material test: %1 x %1 cells, 5mm, pitch 8mm, origin X10 Y10)\n(Rows bottom to "
                "top: F%2 to F%3 mm/min)\n(Columns left to right: P%4 to P%5 percent)\n")
            .arg(steps)
            .arg(minSpeed)
            .arg(maxSpeed)
            .arg(minPower)
            .arg(maxPower);
    auto bytes = (guide + plan.gcode().join("\n") + "\n").toUtf8();
    QSaveFile file(url.toLocalFile());
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        notify("Materialtest konnte nicht gespeichert werden.");
        return false;
    }
    notify("Materialtest exportiert: Zeilen=Geschwindigkeit, Spalten=Leistung; kein automatischer "
           "Start.");
    return true;
}

bool Document::validBudget(const QList<Artwork> &items) {
    if (items.size() > Geometry::objectLimit)
        return false;
    qint64 bytes = 0, pixels = 0;
    for (const auto &a : items) {
        if (a.asset)
            bytes += a.asset->bytes.size();
        if (a.reliefAsset) {
            bytes += a.reliefAsset->png.size();
            pixels += qint64(a.reliefAsset->image.width()) * a.reliefAsset->image.height();
        }
        if (!Geometry::validBounds(a.bounds))
            return false;
    }
    return bytes <= maxEmbeddedBytes && pixels <= maxReliefPixels;
}

QVariantMap Document::beginnerSummary() const {
    bool advanced = false;
    for (const auto &a : m_items)
        if (a.page == m_activePage)
            advanced |= a.layer >= 2 || !a.extraOperations.isEmpty() || a.kind == "relief";
    for (int i = 0; i < 2; ++i) {
        const auto values = m_layers[i].toMap();
        for (const QString key : {"angle", "angleIncrement", "kerf", "scanOffset", "tabs",
                                  "startPoint", "leadIn", "leadOut", "overscan"})
            advanced |= std::abs(values.value(key, 0).toDouble()) > .000001;
        advanced |= values.value("crosshatch", false).toBool();
    }
    bool modified = materialModified();
    QString reason = advanced
                         ? "Dieses Projekt verwendet erweiterte Bearbeitungswerte, zusätzliche "
                           "Operationen oder Reliefs. Bitte im Advanced-Modus prüfen."
                     : modified ? "Die Bearbeitungswerte weichen vom Materialprofil ab. Profil neu "
                                  "übernehmen oder Advanced verwenden."
                                : QString();
    return {{"requiresAdvanced", advanced},
            {"modified", modified},
            {"reason", reason},
            {"source", m_material.value("source", QString("Eigene Werte"))},
            {"material", m_material.value("name", QString("Kein Material"))}};
}
bool Document::applyBasicMaterial(const QVariantMap &profile) {
    if (beginnerSummary()["requiresAdvanced"].toBool()) {
        notify("Erweiterte Bearbeitungswerte, zusätzliche Operationen oder Reliefs bitte in "
               "Advanced bearbeiten.");
        return false;
    }
    bool engrave = profile["engrave"].toMap()["enabled"].toBool(),
         cut = profile["cut"].toMap()["enabled"].toBool();
    if (cut && !engrave)
        for (const auto &a : m_items)
            if (a.page == m_activePage && a.kind == "image") {
                notify("Ein Foto benötigt ein Gravurprofil, kein Schnittprofil.");
                return false;
            }
    const bool profileChanges =
        m_material != profile || m_layers != materialLayers(m_layers, profile);
    if (!applyMaterial(profile))
        return false;
    bool change = false;
    for (const auto &a : m_items)
        if (a.page == m_activePage)
            change |= !a.settings.isEmpty() || (engrave != cut && a.layer != (engrave ? 0 : 1));
    if (change) {
        if (!profileChanges)
            checkpoint("Profilwerte übernehmen");
        for (auto &a : m_items)
            if (a.page == m_activePage) {
                a.settings.clear();
                a.relief.depthPerPass = 0;
                if (engrave != cut)
                    a.layer = engrave ? 0 : 1;
            }
        emit dataChanged(index(0), index(m_items.size() - 1));
        refresh();
    }
    return true;
}

void Document::setBackgroundPlanning(bool enabled) {
    if (m_background == enabled)
        return;
    m_background = enabled;
    m_job.reset();
    if (enabled)
        m_preparation.submit(jobInput());
    else
        m_preparation.cancel();
    emit changed();
}
JobInput Document::jobInput() const {
    JobInput result{m_items, m_layers, m_activePage};
    // Do not transfer GUI-owned QSvgRenderer instances, even by shared_ptr.
    QHash<const SvgAsset *, std::shared_ptr<const SvgAsset>> assets;
    for (auto &art : result.items)
        if (art.asset) {
            auto key = art.asset.get();
            if (!assets.contains(key)) {
                auto copy = std::make_shared<SvgAsset>();
                copy->bytes = art.asset->bytes;
                copy->sizeMm = art.asset->sizeMm;
                assets.insert(key, copy);
            }
            art.asset = assets[key];
        }
    return result;
}
QVariantMap Document::jobSummary() const {
    auto summary = job().summary();
    summary["state"] = planningState();
    summary["busy"] = planningBusy();
    return summary;
}
QString Document::planningState() const {
    return m_background ? m_preparation.state() : "synchronous";
}
bool Document::planningBusy() const {
    return m_background && (m_preparation.busy() || m_batch.busy());
}
void Document::cancelPlanning() {
    m_batch.cancel();
    if (m_background)
        m_preparation.cancel();
}
void Document::retryPlanning() {
    if (m_background)
        m_preparation.submit(jobInput());
}
QImage Document::imagePreview(const Artwork &item) const {
    const auto &plan = job();
    return plan.previews.value(item.id, item.reliefAsset ? item.reliefAsset->image : QImage());
}
