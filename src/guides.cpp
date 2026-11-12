#include "document.h"
#include "geometry.h"
#include <QUuid>
#include <algorithm>
#include <cmath>

QVariantList Document::guides() const {
    QVariantList result;
    for (const auto &guide : m_guides)
        if (guide.page == m_activePage)
            result.append(QVariantMap{{"id", guide.id},
                                      {"axis", guide.axis},
                                      {"position", guide.position},
                                      {"locked", guide.locked}});
    return result;
}
void Document::refreshGuides() {
    m_dirty = !sameContent(snapshot(), m_saved);
    emit changed();
}
QString Document::addGuide(const QString &axis, double position) {
    if ((axis != "vertical" && axis != "horizontal") || !std::isfinite(position) ||
        std::abs(position) > Geometry::coordinateLimit || m_guides.size() >= 1000) {
        notify("Hilfslinie ungültig oder Grenze von 1000 Linien erreicht.");
        return {};
    }
    checkpoint("Hilfslinie hinzufügen");
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_guides.append({id, axis, position, m_activePage, false});
    m_guidesVisible = true;
    refreshGuides();
    return id;
}
bool Document::setGuidePosition(const QString &id, double position) {
    if (!std::isfinite(position) || std::abs(position) > Geometry::coordinateLimit)
        return false;
    for (int i = 0; i < m_guides.size(); ++i)
        if (m_guides[i].id == id && m_guides[i].page == m_activePage) {
            if (m_guides[i].locked) {
                notify("Gesperrte Hilfslinie zuerst entsperren.");
                return false;
            }
            if (m_guides[i].position == position)
                return true;
            checkpoint("Hilfslinie verschieben");
            m_guides[i].position = position;
            refreshGuides();
            return true;
        }
    return false;
}
bool Document::setGuideLocked(const QString &id, bool locked) {
    for (int i = 0; i < m_guides.size(); ++i)
        if (m_guides[i].id == id && m_guides[i].page == m_activePage) {
            if (m_guides[i].locked == locked)
                return true;
            checkpoint(locked ? "Hilfslinie sperren" : "Hilfslinie entsperren");
            m_guides[i].locked = locked;
            refreshGuides();
            return true;
        }
    return false;
}
bool Document::removeGuide(const QString &id) {
    for (int i = 0; i < m_guides.size(); ++i)
        if (m_guides[i].id == id && m_guides[i].page == m_activePage) {
            if (m_guides[i].locked) {
                notify("Gesperrte Hilfslinie zuerst entsperren.");
                return false;
            }
            checkpoint("Hilfslinie löschen");
            m_guides.removeAt(i);
            refreshGuides();
            return true;
        }
    return false;
}
void Document::clearGuides() {
    if (std::none_of(m_guides.cbegin(), m_guides.cend(),
                     [this](const Guide &g) { return g.page == m_activePage; }))
        return;
    checkpoint("Hilfslinien auf Blatt löschen");
    m_guides.removeIf([this](const Guide &g) { return g.page == m_activePage; });
    refreshGuides();
}
void Document::setGuidesVisible(bool visible) {
    if (m_guidesVisible == visible)
        return;
    checkpoint("Hilfslinien anzeigen");
    m_guidesVisible = visible;
    refreshGuides();
}
void Document::setGuideSnapping(bool enabled) {
    if (m_guideSnapping == enabled)
        return;
    checkpoint("Hilfslinienfang");
    m_guideSnapping = enabled;
    refreshGuides();
}
