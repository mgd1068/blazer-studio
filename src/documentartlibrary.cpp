#include "document.h"
#include "geometry.h"
#include "geometryassetcodec.h"
#include <QScopeGuard>
#include <QSet>
#include <cmath>

bool Document::insertArtGeometry(const QByteArray &geometry, double xMm, double yMm) {
    static thread_local QSet<const Document *> activeInsertions;
    if (activeInsertions.contains(this))
        return false;
    activeInsertions.insert(this);
    const auto insertionGuard = qScopeGuard([this] { activeInsertions.remove(this); });
    const auto fail = [this](const QString &message) {
        notify(message);
        return false;
    };
    if (m_beforeMove)
        return fail("Ziehen zuerst beenden; keine Grafik eingefügt.");
    if (planningBusy() || batchBusy() || imageExportBusy())
        return fail("Berechnung oder Export abwarten; keine Grafik eingefügt.");
    if (!std::isfinite(xMm) || !std::isfinite(yMm))
        return fail("Einfügeposition muss aus endlichen Millimeterwerten bestehen.");
    auto decoded = GeometryAssetCodec::decode(geometry);
    if (!decoded.valid())
        return fail(decoded.error);
    const QPointF offset(xMm, yMm);
    const QRectF target(offset, decoded.boundsMm.size());
    if (!Geometry::insideBed(target, workArea()))
        return fail("Grafik passt an dieser Position nicht vollständig auf die Arbeitsfläche; "
                    "keine Skalierung oder Klemmung.");
    auto proposed = snapshot();
    const int first = proposed.items.size();
    QList<int> pending;
    for (int i = 0; i < decoded.items.size(); ++i) {
        auto &art = decoded.items[i];
        art.bounds.translate(offset);
        if (!Geometry::insideBed(art.bounds, workArea()) || art.toolLayer < 1 ||
            art.toolLayer > 2 || !art.settings.isEmpty() || !art.extraOperations.isEmpty())
            return fail(
                "Grafikobjekt überschreitet Geometrie- oder Vorlagenvertrag; kein Teilimport.");
        art.page = m_activePage;
        if (decoded.sourceRoles[i] == "graphic")
            pending.append(first + i);
        proposed.items.append(art);
    }
    proposed.selection = pending;
    proposed.selected = pending.isEmpty() ? -1 : pending.first();
    qint64 elements = 0;
    for (const auto &art : proposed.items)
        elements += art.path.elementCount();
    if (elements > 1000000 || !validBudget(proposed.items))
        return fail("Einfügen überschreitet Projekt-, Bild- oder Pfadbudgets; kein Teilimport.");
    const auto validated = parse(serialize(proposed));
    if (!validated)
        return fail("Gesamter vorgeschlagener Projektstand ist ungültig; kein Teilimport.");
    checkpoint("Bibliotheksgrafik einfügen");
    beginResetModel();
    // Only artwork/selection change. Applying an entire snapshot would reset
    // existing runtime origin confirmation although no placement intent changed.
    m_items = validated->items;
    m_selected = validated->selected;
    m_selection = validated->selection;
    endResetModel();
    refresh();
    notify(
        "Grafik als nicht ausgebende Vorlage eingefügt. Eigene Bearbeitung ausdrücklich wählen.");
    return true;
}
