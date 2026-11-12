#include "document.h"
#include "geometry.h"
#include "imagelimits.h"
#include "imageprocessing.h"
#include "vectorutil.h"
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainterPathStroker>
#include <QSaveFile>
#include <QTransform>
#include <QUuid>
#include <QXmlStreamWriter>
#include <algorithm>
#include <cmath>
#include <qrcodegen/qrcodegen.hpp>

namespace {
bool validPageName(const QString &name) {
    return !name.trimmed().isEmpty() && name.size() <= 200 && !name.contains('\n') &&
           !name.contains('\r');
}
Artwork pageCopy(const Artwork &source, int page, QMap<QString, QString> &groups) {
    auto copy = source;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.page = page;
    if (!source.groupId.isEmpty()) {
        if (!groups.contains(source.groupId))
            groups[source.groupId] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        copy.groupId = groups[source.groupId];
    }
    return copy;
}
} // namespace

bool Document::bakePath(Artwork &a, const QPainterPath &path) {
    if (path.isEmpty() || path.elementCount() > 100000)
        return false;
    auto b = path.boundingRect();
    if (b.width() < .1)
        b.adjust(-.05, 0, .05, 0);
    if (b.height() < .1)
        b.adjust(0, -.05, 0, .05);
    if (!Geometry::validBounds(b))
        return false;
    for (int i = 0; i < path.elementCount(); ++i) {
        auto e = path.elementAt(i);
        if (!std::isfinite(e.x) || !std::isfinite(e.y))
            return false;
    }
    QTransform t;
    t.scale(1 / b.width(), 1 / b.height());
    t.translate(-b.x(), -b.y());
    a.path = t.map(path);
    a.bounds = b;
    a.kind = "path";
    a.asset.reset();
    a.reliefAsset.reset();
    return true;
}
QVariantList Document::nodes() const {
    QVariantList out;
    if (m_selection.size() != 1)
        return out;
    const auto &a = m_items[m_selected];
    if (a.kind != "path")
        return out;
    QString error;
    auto p = JobPlanner::geometry(a, error);
    for (int i = 0; i < p.elementCount(); ++i) {
        auto e = p.elementAt(i);
        out.append(QVariantMap{{"index", i}, {"type", int(e.type)}, {"x", e.x}, {"y", e.y}});
    }
    return out;
}
bool Document::setNode(int element, double x, double y) {
    if (m_selection.size() != 1 || m_items[m_selected].kind != "path" || !std::isfinite(x) ||
        !std::isfinite(y))
        return false;
    auto a = m_items[m_selected];
    QString error;
    auto p = JobPlanner::geometry(a, error);
    if (element < 0 || element >= p.elementCount())
        return false;
    p.setElementPositionAt(element, x, y);
    if (!bakePath(a, p))
        return false;
    if (!m_beforeMove)
        checkpoint();
    m_items[m_selected] = a;
    refresh(m_selected);
    return true;
}
int Document::addOperation(const QString &name, const QString &mode) {
    if (m_layers.size() >= 64 || name.trimmed().isEmpty() || name.size() > 200 ||
        !QStringList{"Gravur", "Schnitt"}.contains(mode))
        return -1;
    checkpoint();
    auto map = m_layers[mode == "Schnitt" ? 1 : 0].toMap();
    map["name"] = name;
    map["mode"] = mode;
    m_layers.append(map);
    refresh();
    return m_layers.size() - 1;
}
void Document::assignOperations(const QVariantList &ops) {
    if (m_selection.isEmpty() || ops.isEmpty() || ops.size() > 64)
        return;
    QList<int> rows;
    for (auto v : ops) {
        bool ok;
        double n = v.toDouble(&ok);
        if (!ok || !std::isfinite(n) || n != int(n) || n < 0 || n >= m_layers.size() ||
            rows.contains(int(n)))
            return;
        for (int selected : m_selection)
            if ((m_items[selected].kind == "relief" || m_items[selected].kind == "image") &&
                m_layers[int(n)].toMap()["mode"] == "Schnitt")
                return;
        rows.append(int(n));
    }
    checkpoint();
    for (int row : m_selection) {
        m_items[row].layer = rows.first();
        m_items[row].extraOperations = rows.mid(1);
    }
    emit dataChanged(index(0), index(m_items.size() - 1));
    refresh();
}
bool Document::addPage(const QString &name) {
    if (m_pages.size() >= 100 || !validPageName(name)) {
        notify("Bitte einen einzeiligen Blattnamen mit 1–200 Zeichen wählen; maximal 100 Blätter.");
        return false;
    }
    checkpoint();
    m_pages.append(name.trimmed());
    m_activePage = m_pages.size() - 1;
    m_selection.clear();
    m_selected = -1;
    if (!m_items.isEmpty())
        emit dataChanged(index(0), index(m_items.size() - 1));
    refresh();
    return true;
}
bool Document::copyPage(int page, const QString &name) {
    if (page < 0 || page >= m_pages.size() || m_pages.size() >= 100 || !validPageName(name)) {
        notify("Blattkopie benötigt ein vorhandenes Blatt, einen einzeiligen Namen mit 1–200 "
               "Zeichen und Platz für ein weiteres Blatt (maximal 100).");
        return false;
    }
    auto result = m_items;
    QMap<QString, QString> groups;
    for (const auto &item : m_items)
        if (item.page == page)
            result.append(pageCopy(item, m_pages.size(), groups));
    if (!validBudget(result)) {
        notify("Blattkopie überschreitet die Objekt- oder Einbettungsgrenzen des Projekts.");
        return false;
    }
    checkpoint();
    beginResetModel();
    m_items = std::move(result);
    m_pages.append(name.trimmed());
    m_activePage = m_pages.size() - 1;
    m_selection.clear();
    m_selected = -1;
    normalizeGroups();
    endResetModel();
    refresh();
    notify("Blatt kopiert und zur Anzeige/Ausgabe gewählt. Operationen gelten projektweit.");
    return true;
}
void Document::activatePage(int page) {
    if (page < 0 || page >= m_pages.size() || page == m_activePage)
        return;
    finishMove();
    m_activePage = page;
    m_selection.clear();
    m_selected = -1;
    m_job.reset();
    if (m_background)
        m_preparation.submit(jobInput());
    if (!m_items.isEmpty())
        emit dataChanged(index(0), index(m_items.size() - 1));
    emit changed();
    emit selectionChanged();
}
bool Document::renamePage(int page, const QString &name) {
    if (page < 0 || page >= m_pages.size() || !validPageName(name)) {
        notify("Bitte ein vorhandenes Blatt und einen einzeiligen Blattnamen mit 1–200 Zeichen "
               "wählen.");
        return false;
    }
    if (m_pages[page] == name.trimmed())
        return true;
    checkpoint();
    m_pages[page] = name.trimmed();
    refresh();
    return true;
}
bool Document::restoreBytes(const QByteArray &bytes) {
    auto state = parse(bytes);
    if (!state) {
        notify("Wiederherstellung abgelehnt: ungültiges Projekt.");
        return false;
    }
    checkpoint();
    m_saved = Snapshot{};
    setFileUrl({});
    applySnapshot(*state);
    notify("Projekt wiederhergestellt; bitte separat speichern.");
    return true;
}
bool Document::command(const QString &action, const QVariantMap &args) {
    auto fail = [&](const QString &message) {
        notify(message);
        return false;
    };
    if (action == "qr") {
        auto text = args.value("text").toString();
        if (text.isEmpty() || text.toUtf8().size() > 1000)
            return false;
        try {
            auto code = qrcodegen::QrCode::encodeText(text.toUtf8().constData(),
                                                      qrcodegen::QrCode::Ecc::MEDIUM);
            Artwork a;
            a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            a.name = "QR: " + text.left(40);
            a.style["qr"] = true;
            a.kind = "path";
            a.layer = 0;
            a.bounds = QRectF(10, 10, 30, 30);
            a.path.setFillRule(Qt::WindingFill);
            double size = code.getSize() + 8;
            for (int y = 0; y < code.getSize(); ++y)
                for (int x = 0; x < code.getSize(); ++x)
                    if (code.getModule(x, y))
                        a.path.addRect(QRectF((x + 4) / size, (code.getSize() - 1 - y + 4) / size,
                                              1 / size, 1 / size));
            if (!canAppend(a))
                return false;
            appendArtwork(a);
            return true;
        } catch (const std::exception &e) {
            return fail(QString::fromUtf8(e.what()));
        }
    }
    if (action == "addLine" || action == "addBezier") {
        Artwork a;
        a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        a.name = action == "addLine" ? "Linie" : "Bézier";
        a.layer = 1;
        QPainterPath p;
        p.moveTo(10, 10);
        if (action == "addLine")
            p.lineTo(50, 30);
        else
            p.cubicTo(10, 50, 50, 0, 60, 35);
        if (!bakePath(a, p) || !canAppend(a))
            return false;
        appendArtwork(a);
        return true;
    }
    if (action == "calibration") {
        QString kind = args.value("kind", "scale").toString();
        if (kind != "scale" && kind != "interval" && kind != "focus")
            return false;
        int count = kind == "scale" ? 1 : 5;
        if (m_items.size() + count * 2 > Geometry::objectLimit || m_layers.size() + count > 64)
            return false;
        checkpoint();
        beginResetModel();
        for (int i = 0; i < count; ++i) {
            Artwork a;
            a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            a.kind = "rect";
            a.page = m_activePage;
            a.bounds = QRectF(10 + i * 20, 10, 10, 10);
            a.name = kind + " " + QString::number(i + 1);
            a.layer = kind == "scale" ? 1 : 0;
            if (kind == "interval") {
                auto op = m_layers[0].toMap();
                op["interval"] = (i + 1) * .05;
                op["name"] = "Raster " + QString::number((i + 1) * .05);
                m_layers.append(op);
                a.layer = m_layers.size() - 1;
            }
            if (kind == "focus") {
                a.kind = "path";
                a.name = "Fokusposition " + QString::number(i + 1);
                a.layer = 1;
                a.path.moveTo(0, .5);
                a.path.lineTo(1, .5);
                a.path.moveTo(.5, 0);
                a.path.lineTo(.5, 1);
            }
            m_items.append(a);
            auto label = a;
            label.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            label.kind = "text";
            label.path = {};
            label.layer = 0;
            label.bounds = QRectF(a.bounds.x(), 23, 15, 3);
            label.text = kind == "interval" ? QString::number((i + 1) * .05, 'f', 2) + " mm"
                         : kind == "scale"  ? "10 mm"
                                            : "F" + QString::number(i + 1);
            m_items.append(label);
        }
        m_selected = m_items.size() - 2;
        m_selection = {m_selected};
        endResetModel();
        refresh();
        notify(kind == "focus" ? "Fokusmarken erzeugt. Fokus manuell zwischen getrennten Proben "
                                 "einstellen; keine automatische Z-Verstellung."
                               : "Kalibriermuster angefügt; keine automatische Ausführung.");
        return true;
    }
    if (m_selection.isEmpty())
        return fail("Zuerst Objekte auswählen.");
    if (action == "imageSettings" || action == "trace" || action == "imageMask" ||
        action == "imageRotate" || action == "imageMirror" || action == "imageToRelief") {
        int row = -1;
        for (int i : m_selection)
            if (m_items[i].kind == "image" || m_items[i].kind == "relief") {
                row = i;
                break;
            }
        if (row < 0)
            return fail("Ein Rasterbild auswählen.");
        auto a = m_items[row];
        if (!a.reliefAsset)
            return false;
        if (action == "imageSettings") {
            if (a.kind != "image")
                return fail("Reliefparameter im Bereich Relief ändern; Bildkorrekturen vor der "
                            "Umwandlung anwenden.");
            const QSet<QString> keys{"brightness", "contrast",  "gamma",  "sharpness",
                                     "imageMode",  "threshold", "invert", "ellipseMask",
                                     "cropX",      "cropY",     "cropW",  "cropH"};
            for (auto it = args.begin(); it != args.end(); ++it)
                if (!keys.contains(it.key()))
                    return fail("Unbekannte Bildeinstellung.");
            auto style = a.style;
            for (auto it = args.begin(); it != args.end(); ++it) {
                if (it.key() == "imageMode") {
                    if (it.value().metaType().id() != QMetaType::QString)
                        return false;
                    style[it.key()] = it.value();
                } else if (it.key() == "invert" || it.key() == "ellipseMask") {
                    if (it.value().metaType().id() != QMetaType::Bool)
                        return false;
                    style[it.key()] = it.value();
                } else {
                    bool ok;
                    double n = it.value().toDouble(&ok);
                    if (!ok || !std::isfinite(n) || it.value().metaType().id() == QMetaType::Bool)
                        return false;
                    if (it.key().startsWith("crop")) {
                        if (n < 0 || n > ImageLimits::side || std::floor(n) != n)
                            return false;
                        style[it.key()] = int(n);
                    } else
                        style[it.key()] = n;
                }
            }
            if (style.size() > 20)
                return false;
            QString error;
            if (!ImageProcessing::validate(a.reliefAsset->image.size(), style, error))
                return fail(error);
            a.style = style;
        } else if (action == "imageRotate" || action == "imageMirror") {
            QString error;
            auto image = a.kind == "image"
                             ? ImageProcessing::apply(a.reliefAsset->image, a.style, error)
                             : a.reliefAsset->image;
            if (!error.isEmpty())
                return fail(error);
            if (action == "imageRotate") {
                QTransform t;
                t.rotate(90);
                image = image.transformed(t);
                a.bounds.setSize(QSizeF(a.bounds.height(), a.bounds.width()));
            } else {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
                image = image.flipped(Qt::Horizontal);
#else
                image = image.mirrored(true, false);
#endif
            }
            a.reliefAsset = ReliefAsset::fromImage(image);
            if (!a.reliefAsset)
                return false;
            if (a.kind == "image")
                a.style = {{"imageMode", a.style.value("imageMode", "dither")}};
        } else if (action == "imageMask") {
            int other = -1;
            for (int i : m_selection)
                if (i != row)
                    other = i;
            if (other < 0)
                return fail("Bild und Maskenform auswählen.");
            QString error;
            auto mask = JobPlanner::geometry(m_items[other], error, true);
            if (!error.isEmpty())
                return fail(error);
            auto image = a.kind == "image"
                             ? ImageProcessing::apply(a.reliefAsset->image, a.style, error)
                             : a.reliefAsset->image;
            if (!error.isEmpty())
                return fail(error);
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    if (!mask.contains(QPointF(
                            a.bounds.x() + (x + .5) * a.bounds.width() / image.width(),
                            a.bounds.bottom() - (y + .5) * a.bounds.height() / image.height())))
                        image.setPixelColor(x, y, Qt::transparent);
            a.reliefAsset = ReliefAsset::fromImage(image);
            if (!a.reliefAsset)
                return false;
            if (a.kind == "image")
                a.style = {{"imageMode", a.style.value("imageMode", "dither")}};
        } else {
            if (action == "imageToRelief" && a.kind == "relief")
                return true;
            QString error;
            auto image = ImageProcessing::apply(a.reliefAsset->image, a.style, error);
            if (!error.isEmpty())
                return fail(error);
            if (action == "imageToRelief") {
                a.kind = "relief";
                a.reliefAsset = ReliefAsset::fromImage(image);
                a.style.clear();
            } else {
                QPainterPath path;
                path.setFillRule(Qt::WindingFill);
                for (int y = 0; y < image.height(); ++y) {
                    int start = -1;
                    for (int x = 0; x <= image.width(); ++x) {
                        bool black = x < image.width() && image.pixelColor(x, y).alpha() > 0 &&
                                     qGray(image.pixel(x, y)) < 128;
                        if (black && start < 0)
                            start = x;
                        if (!black && start >= 0) {
                            path.addRect(QRectF(
                                double(start) / image.width(), 1. - double(y + 1) / image.height(),
                                double(x - start) / image.width(), 1. / image.height()));
                            start = -1;
                        }
                    }
                }
                if (path.elementCount() > 100000)
                    return fail("Bild für Vektorisierung zu komplex; vorher verkleinern.");
                a.path = path.simplified();
                if (a.path.isEmpty())
                    return fail("Keine dunklen Bildbereiche.");
                a.kind = "path";
                a.reliefAsset.reset();
                a.style.clear();
            }
        }
        a.relief.depthPerPass = 0;
        auto candidate = m_items;
        candidate[row] = a;
        if (!validBudget(candidate))
            return fail("Bildänderung überschreitet die Projektgrenzen.");
        checkpoint();
        m_items[row] = a;
        refresh(row);
        return true;
    }
    auto result = m_items;
    auto pageNames = m_pages;
    auto box = selectionBounds();
    if (action == "rotate" || action == "mirrorX" || action == "mirrorY" || action == "convert" ||
        action == "simplify" || action == "offset" || action == "closePath" ||
        action == "reverse" || action == "split") {
        double amount = args.value("value", 0).toDouble();
        if (!std::isfinite(amount) || std::abs(amount) > 3600)
            return false;
        QTransform t;
        t.translate(box.center().x(), box.center().y());
        if (action == "rotate")
            t.rotate(amount);
        if (action == "mirrorX")
            t.scale(-1, 1);
        if (action == "mirrorY")
            t.scale(1, -1);
        t.translate(-box.center().x(), -box.center().y());
        for (int row : m_selection) {
            auto &a = result[row];
            if (a.kind == "relief" || a.kind == "image")
                return fail("Rasterbilder mit Bildwerkzeugen drehen oder spiegeln.");
            QString error;
            auto p = JobPlanner::geometry(
                a, error, a.layer >= 0 && m_layers[a.layer].toMap()["mode"] != "Schnitt");
            if (!error.isEmpty())
                return fail(error);
            if (action == "simplify")
                p = p.simplified();
            else if (action == "closePath")
                p.closeSubpath();
            else if (action == "reverse")
                p = p.toReversed();
            else if (action == "offset") {
                QPainterPathStroker stroker;
                stroker.setWidth(std::abs(amount) * 2);
                stroker.setJoinStyle(Qt::RoundJoin);
                auto stroke = stroker.createStroke(p);
                p = amount >= 0 ? p.united(stroke) : p.subtracted(stroke);
            } else
                p = t.map(p);
            if (!bakePath(a, p))
                return fail("Bearbeitung erzeugt ungültige Geometrie.");
        }
        if (action == "split") {
            if (m_selection.size() != 1)
                return fail("Ein Pfadobjekt auswählen.");
            auto base = result[m_selected];
            QString error;
            auto p = JobPlanner::geometry(base, error);
            QPainterPath current;
            QList<Artwork> pieces;
            for (int i = 0; i < p.elementCount(); ++i) {
                auto e = p.elementAt(i);
                if (e.isMoveTo()) {
                    if (!current.isEmpty()) {
                        auto a = base;
                        a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                        a.groupId.clear();
                        a.groupName.clear();
                        if (bakePath(a, current))
                            pieces.append(a);
                    }
                    current = QPainterPath();
                    current.moveTo(e.x, e.y);
                } else if (e.isLineTo())
                    current.lineTo(e.x, e.y);
                else {
                    auto b = p.elementAt(++i), c = p.elementAt(++i);
                    current.cubicTo(e.x, e.y, b.x, b.y, c.x, c.y);
                }
            }
            if (!current.isEmpty()) {
                auto a = base;
                a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                a.groupId.clear();
                a.groupName.clear();
                if (bakePath(a, current))
                    pieces.append(a);
            }
            if (result.size() + pieces.size() - 1 > Geometry::objectLimit)
                return false;
            result.removeAt(m_selected);
            for (auto &a : pieces)
                result.append(a);
        }
    } else if (action == "union" || action == "difference" || action == "intersection" ||
               action == "join") {
        if (m_selection.size() < 2)
            return fail("Mindestens zwei Objekte auswählen.");
        QPainterPath merged;
        bool first = true;
        for (int row : m_selection) {
            QString error;
            auto p = JobPlanner::geometry(result[row], error, true);
            if (!error.isEmpty())
                return fail(error);
            if (first)
                merged = p;
            else if (action == "union")
                merged = merged.united(p);
            else if (action == "difference")
                merged = merged.subtracted(p);
            else if (action == "intersection")
                merged = merged.intersected(p);
            else
                merged.connectPath(p);
            first = false;
        }
        auto a = result[m_selected];
        if (!bakePath(a, merged))
            return fail("Ergebnis ist leer oder ungültig.");
        a.groupId.clear();
        a.groupName.clear();
        auto rows = m_selection;
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        for (int row : rows)
            result.removeAt(row);
        result.append(a);
    } else if (action.startsWith("align") || action == "distributeX" || action == "distributeY") {
        if (m_selection.size() < 2)
            return fail("Mindestens zwei Objekte auswählen.");
        auto rows = m_selection;
        bool x = action.endsWith("X");
        if (action.startsWith("distribute"))
            std::stable_sort(rows.begin(), rows.end(), [&](int a, int b) {
                return x ? result[a].bounds.center().x() < result[b].bounds.center().x()
                         : result[a].bounds.center().y() < result[b].bounds.center().y();
            });
        double first =
            x ? result[rows.first()].bounds.center().x() : result[rows.first()].bounds.center().y();
        double last =
            x ? result[rows.last()].bounds.center().x() : result[rows.last()].bounds.center().y();
        for (int i = 0; i < rows.size(); ++i) {
            auto &b = result[rows[i]].bounds;
            if (action == "alignLeft")
                b.moveLeft(box.left());
            else if (action == "alignRight")
                b.moveRight(box.right());
            else if (action == "alignBottom")
                b.moveTop(box.top());
            else if (action == "alignTop")
                b.moveBottom(box.bottom());
            else if (action == "alignCenterX")
                b.moveCenter({box.center().x(), b.center().y()});
            else if (action == "alignCenterY")
                b.moveCenter({b.center().x(), box.center().y()});
            else if (action == "distributeX")
                b.moveCenter({first + (last - first) * i / (rows.size() - 1), b.center().y()});
            else if (action == "distributeY")
                b.moveCenter({b.center().x(), first + (last - first) * i / (rows.size() - 1)});
        }
    } else if (action == "textStyle") {
        QString font = args.value("font", "DejaVu Sans").toString();
        double spacing = args.value("spacing", 0).toDouble();
        if (font.size() > 200 || !std::isfinite(spacing) || std::abs(spacing) > 100)
            return false;
        for (int row : m_selection) {
            if (result[row].kind != "text")
                continue;
            result[row].style = {
                {"font", font}, {"spacing", spacing}, {"bold", args.value("bold", false)}};
        }
    } else if (action == "textOnPath") {
        if (m_selection.size() != 2)
            return fail("Ein Textobjekt und einen Pfad auswählen.");
        int tr = -1, pr = -1;
        for (int row : m_selection)
            if (result[row].kind == "text")
                tr = row;
            else
                pr = row;
        if (tr < 0 || pr < 0)
            return false;
        QString error;
        auto curve = JobPlanner::geometry(result[pr], error);
        double length = curve.length();
        if (length < .1)
            return false;
        QFont font(result[tr].style.value("font", "DejaVu Sans").toString());
        font.setPixelSize(100);
        QPainterPath all;
        const auto text = result[tr].text;
        double step = length / std::max(1, int(text.size()));
        for (int i = 0; i < text.size(); ++i) {
            auto u = curve.percentAtLength((i + .5) * step);
            auto point = curve.pointAtPercent(u);
            QPainterPath glyph;
            glyph.addText(0, 0, font, QString(text[i]));
            auto b = glyph.boundingRect();
            if (b.isEmpty())
                continue;
            QTransform t;
            t.translate(point.x(), point.y());
            t.rotate(curve.angleAtPercent(u));
            t.scale(step / std::max(1., b.width()), -result[tr].bounds.height() / b.height());
            t.translate(-b.center().x(), -b.bottom());
            all.addPath(t.map(glyph));
        }
        if (!bakePath(result[tr], all))
            return false;
    } else if (action == "duplicateGrid" || action == "nest") {
        int columns = args.value("columns", 2).toInt(), copies = args.value("copies", 4).toInt();
        double gap = args.value("gap", 2).toDouble();
        if (columns < 1 || columns > 100 || copies < 1 || copies > 1000 || !std::isfinite(gap) ||
            gap < 0 || gap > 100)
            return false;
        if (action == "nest") {
            QList<QPainterPath> occupied;
            int targetPage = m_activePage;
            for (int i = 0; i < result.size(); ++i)
                if (!m_selection.contains(i) && result[i].page == targetPage && result[i].visible) {
                    QString error;
                    auto p = JobPlanner::geometry(result[i], error, true);
                    if (!error.isEmpty())
                        return fail(error);
                    if (result[i].style.value("qr", false).toBool()) {
                        p = {};
                        p.addRect(result[i].bounds);
                    }
                    occupied.append(p);
                }
            auto rows = m_selection;
            std::stable_sort(rows.begin(), rows.end(), [&](int a, int b) {
                return result[a].bounds.width() * result[a].bounds.height() >
                       result[b].bounds.width() * result[b].bounds.height();
            });
            QSet<int> packed;
            for (int row : rows) {
                if (packed.contains(row))
                    continue;
                QList<int> members{row};
                auto group = result[row].groupId;
                if (!group.isEmpty()) {
                    QList<int> grouped;
                    bool complete = true;
                    for (int i = 0; i < result.size(); ++i)
                        if (result[i].groupId == group) {
                            grouped.append(i);
                            complete &= m_selection.contains(i);
                        }
                    if (complete)
                        members = grouped;
                }
                QMap<int, QPainterPath> actuals;
                QPainterPath shape;
                for (int member : members) {
                    QString error;
                    auto path = JobPlanner::geometry(result[member], error, true);
                    if (!error.isEmpty())
                        return fail(error);
                    actuals[member] = path;
                    bool open = false;
                    for (const auto &poly : path.toSubpathPolygons())
                        if (poly.size() > 1 && QLineF(poly.first(), poly.last()).length() > 1e-6)
                            open = true;
                    QPainterPath footprint = path;
                    if (open || result[member].style.value("qr", false).toBool()) {
                        footprint = {};
                        footprint.addRect(result[member].bounds);
                    }
                    shape = shape.isEmpty() ? footprint : shape.united(footprint);
                }
                bool placed = false;
                for (int sheetAttempt = 0; sheetAttempt < 2 && !placed; ++sheetAttempt) {
                    for (int rotation : {0, 90}) {
                        QTransform rt;
                        rt.rotate(args.value("rotate", false).toBool() ? rotation : 0);
                        auto rotated = rt.map(shape);
                        auto rb = rotated.boundingRect();
                        QList<QPointF> candidates{{gap, gap}};
                        for (const auto &used : occupied) {
                            auto b = used.boundingRect();
                            candidates.append({b.right() + gap + .001, b.top()});
                            candidates.append({b.left(), b.bottom() + gap + .001});
                            for (const auto &poly : used.toSubpathPolygons())
                                for (auto point : poly)
                                    if (candidates.size() < 2000)
                                        candidates.append(point + QPointF(gap, gap));
                        }
                        std::stable_sort(candidates.begin(), candidates.end(), [](auto a, auto b) {
                            return a.y() == b.y() ? a.x() < b.x() : a.y() < b.y();
                        });
                        for (auto c : candidates) {
                            auto delta = c - rb.topLeft();
                            auto q = rotated.translated(delta);
                            auto bounds = q.boundingRect();
                            if (!Geometry::insideBed(bounds))
                                continue;
                            QPainterPathStroker stroker;
                            stroker.setWidth(2 * gap);
                            auto expanded = gap > 1e-6 ? q.united(stroker.createStroke(q)) : q;
                            bool collision = false;
                            for (const auto &used : occupied)
                                if (expanded.intersects(used)) {
                                    collision = true;
                                    break;
                                }
                            if (collision)
                                continue;
                            QList<QPair<int, Artwork>> changes;
                            bool valid = true;
                            for (int member : members) {
                                auto a = result[member];
                                auto path = rt.map(actuals[member]).translated(delta);
                                if (a.style.value("qr", false).toBool()) {
                                    auto b = rt.mapRect(a.bounds).translated(delta);
                                    QTransform norm;
                                    norm.scale(1 / b.width(), 1 / b.height());
                                    norm.translate(-b.x(), -b.y());
                                    a.path = norm.map(path);
                                    a.bounds = b;
                                } else if (!bakePath(a, path)) {
                                    valid = false;
                                    break;
                                }
                                a.page = targetPage;
                                if (members.size() == 1) {
                                    a.groupId.clear();
                                    a.groupName.clear();
                                }
                                changes.append({member, a});
                            }
                            if (!valid)
                                continue;
                            for (const auto &change : changes) {
                                result[change.first] = change.second;
                                packed.insert(change.first);
                            }
                            occupied.append(q);
                            placed = true;
                            break;
                        }
                        if (placed || !args.value("rotate", false).toBool())
                            break;
                    }
                    if (!placed && args.value("multipage", false).toBool() && sheetAttempt == 0 &&
                        pageNames.size() < 100) {
                        pageNames.append("Nesting " + QString::number(pageNames.size() + 1));
                        targetPage = pageNames.size() - 1;
                        occupied.clear();
                    } else if (!placed)
                        break;
                }
                if (!placed)
                    return fail("Teile passen nicht auf die erlaubten Blätter; Stand unverändert.");
            }
        } else {
            if (result.size() + (copies - 1) * m_selection.size() > Geometry::objectLimit)
                return false;
            for (int copy = 1; copy < copies; ++copy)
                for (int row : m_selection) {
                    auto a = result[row];
                    a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    a.groupId.clear();
                    a.groupName.clear();
                    a.bounds.translate((copy % columns) * (box.width() + gap),
                                       (copy / columns) * (box.height() + gap));
                    if (!Geometry::validBounds(a.bounds))
                        return false;
                    result.append(a);
                }
        }
    } else if (action == "copyToPage") {
        int page = args.value("page", -1).toInt();
        if (page < 0 || page >= m_pages.size() ||
            result.size() + m_selection.size() > Geometry::objectLimit)
            return false;
        QMap<QString, QString> groups;
        for (int row : m_selection)
            result.append(pageCopy(m_items[row], page, groups));
    } else
        return fail("Unbekanntes Werkzeug: " + action);
    if (!validBudget(result))
        return fail("Bearbeitung überschreitet die Projektgrenzen.");
    auto selected = m_selection;
    if (action == "nest") {
        selected.clear();
        for (int row : m_selection)
            if (result[row].page == m_activePage)
                selected.append(row);
    }
    int primary =
        selected.contains(m_selected) ? m_selected : (selected.isEmpty() ? -1 : selected.first());
    if (action == "union" || action == "difference" || action == "intersection" ||
        action == "join" || action == "split") {
        primary = result.size() - 1;
        selected = {primary};
    }
    checkpoint();
    beginResetModel();
    m_items = result;
    m_pages = pageNames;
    normalizeGroups();
    m_selected = primary;
    m_selection = selected;
    endResetModel();
    refresh();
    return true;
}
bool Document::exportSvg(const QUrl &url) const {
    if (!url.isLocalFile())
        return false;
    QByteArray bytes;
    QXmlStreamWriter out(&bytes);
    out.writeStartDocument();
    out.writeStartElement("svg");
    out.writeDefaultNamespace("http://www.w3.org/2000/svg");
    out.writeAttribute("width", "200mm");
    out.writeAttribute("height", "150mm");
    out.writeAttribute("viewBox", "0 0 200 150");
    for (const auto &a : m_items)
        if (a.page == m_activePage && a.visible) {
            QString error;
            auto path = JobPlanner::geometry(a, error, true);
            if (!error.isEmpty())
                return false;
            QTransform t;
            t.translate(0, 150);
            t.scale(1, -1);
            out.writeStartElement("path");
            out.writeAttribute("d", VectorUtil::svg(t.map(path)));
            out.writeAttribute("fill-rule",
                               path.fillRule() == Qt::OddEvenFill ? "evenodd" : "nonzero");
            out.writeAttribute("fill", m_layers[a.layer].toMap()["mode"] == "Schnitt"
                                           ? "none"
                                           : layerColor(a.layer).name());
            out.writeAttribute("stroke", layerColor(a.layer).name());
            out.writeAttribute("stroke-width", "0.1");
            out.writeEndElement();
        }
    out.writeEndElement();
    out.writeEndDocument();
    QSaveFile f(url.toLocalFile());
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
}

bool Document::manageOperation(int row, const QString &action) {
    if (row < 2 || row >= m_layers.size())
        return false;
    if (action == "delete") {
        checkpoint();
        auto mode = m_layers[row].toMap()["mode"].toString();
        m_layers.removeAt(row);
        for (auto &a : m_items) {
            if (a.layer == row)
                a.layer = mode == "Schnitt" ? 1 : 0;
            else if (a.layer > row)
                --a.layer;
            QList<int> extras;
            for (int op : a.extraOperations) {
                if (op == row)
                    continue;
                if (op > row)
                    --op;
                if (op != a.layer && !extras.contains(op))
                    extras.append(op);
            }
            a.extraOperations = extras;
        }
    } else {
        int next = row + (action == "up" ? -1 : action == "down" ? 1 : 0);
        if (next == row || next < 2 || next >= m_layers.size())
            return false;
        checkpoint();
        std::swap(m_layers[row], m_layers[next]);
        auto swap = [&](int op) { return op == row ? next : op == next ? row : op; };
        for (auto &a : m_items) {
            a.layer = swap(a.layer);
            for (auto &op : a.extraOperations)
                op = swap(op);
        }
    }
    emit dataChanged(index(0), index(m_items.size() - 1));
    refresh();
    return true;
}

bool Document::removePage(int page) {
    if (page < 0 || page >= m_pages.size() || m_pages.size() < 2)
        return false;
    checkpoint();
    beginResetModel();
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (m_items[i].page == page)
            m_items.removeAt(i);
        else if (m_items[i].page > page)
            --m_items[i].page;
    }
    m_pages.removeAt(page);
    if (m_activePage > page)
        --m_activePage;
    else if (m_activePage == page)
        m_activePage = std::min(page, int(m_pages.size() - 1));
    m_selected = -1;
    m_selection.clear();
    normalizeGroups();
    endResetModel();
    refresh();
    return true;
}
bool Document::outputs(const Artwork &a) const {
    if (!a.visible || a.page != m_activePage)
        return false;
    QList<int> operations{a.layer};
    operations.append(a.extraOperations);
    for (int op : operations) {
        auto p = m_layers[op].toMap();
        double power = op == a.layer ? a.settings.value("power", p["power"]).toDouble()
                                     : p["power"].toDouble();
        if (p["enabled"].toBool() && power > 0)
            return true;
    }
    return false;
}
