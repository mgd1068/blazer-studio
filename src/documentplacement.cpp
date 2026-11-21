#include "document.h"
#include <QCryptographicHash>
#include <QPointer>

QStringList Document::selectedArtworkIds() const {
    QStringList ids;
    for (const int row : m_selection)
        if (row >= 0 && row < m_items.size())
            ids.append(m_items[row].id);
    ids.sort();
    return ids;
}
void Document::selectionPlanningChanged() {
    if (cutSelectedGraphics() && selectedArtworkIds() != m_plannedSelectedIds) {
        refreshPlanning();
        emit changed();
    }
}
void Document::setCutSelectedGraphics(bool enabled) {
    if (cutSelectedGraphics() == enabled)
        return;
    checkpoint("Nur ausgewählte Grafik ausgeben");
    m_placementIntent.cutSelectedGraphics = enabled;
    refresh();
}
void Document::setUseSelectionOrigin(bool enabled) {
    if (useSelectionOrigin() == enabled)
        return;
    checkpoint("Auswahl als Ursprungsbezug");
    m_placementIntent.useSelectionOrigin = enabled;
    refresh();
}
void Document::refreshPlanning() {
    m_plannedSelectedIds = selectedArtworkIds();
    ++m_framingRevision;
    m_job.reset();
    m_syncPlacementSummary.clear();
    if (m_background)
        m_preparation.submit(planningInput());
}
PlacedJobPlanning::Input Document::planningInput() const {
    return {jobInput(), {m_placementIntent, m_previewPosition, m_confirmedUserOrigin},
            {m_manualRegistration, offlineRegistrationFrame()}};
}
bool Document::setStartMode(StartMode mode) {
    if (mode < AbsoluteCoordinates || mode > UserOrigin) {
        notify("Unbekannter Startmodus.");
        return false;
    }
    if (startMode() == mode)
        return true;
    checkpoint("Startmodus");
    m_placementIntent.mode = JobPlacement::Mode(mode);
    refresh();
    return true;
}
bool Document::setJobOrigin(JobOrigin anchor) {
    if (anchor < LowerLeft || anchor > UpperRight) {
        notify("Ungültiger Neunpunkt-Ursprung.");
        return false;
    }
    if (jobOrigin() == anchor)
        return true;
    checkpoint("Auftragsursprung");
    m_placementIntent.anchor = JobPlacement::Anchor(anchor);
    refresh();
    return true;
}
bool Document::setPreviewPosition(const JobPlacement::PositionSnapshot &position) {
    if (!PlacedJobPlanning::validPosition(position, m_deviceProfile)) {
        clearPreviewPosition();
        notify("Vorschauposition ist unbekannt, veraltet oder gehört nicht zum Projektprofil.");
        return false;
    }
    if (m_previewPosition && m_previewPosition->point.x() == position.point.x() &&
        m_previewPosition->point.y() == position.point.y() &&
        m_previewPosition->known == position.known &&
        m_previewPosition->revision == position.revision &&
        m_previewPosition->profileFingerprint == position.profileFingerprint &&
        m_previewPosition->coordinateFrameFingerprint == position.coordinateFrameFingerprint)
        return true;
    if (!m_previewPosition ||
        m_previewPosition->coordinateFrameFingerprint != position.coordinateFrameFingerprint ||
        m_previewPosition->profileFingerprint != position.profileFingerprint)
        m_confirmedUserOrigin.reset();
    m_previewPosition = position;
    refreshPlanning();
    emit changed();
    return true;
}
void Document::clearPreviewPosition() {
    if (!m_previewPosition && !m_confirmedUserOrigin)
        return;
    m_previewPosition.reset();
    m_confirmedUserOrigin.reset();
    refreshPlanning();
    emit changed();
}
bool Document::captureUserOrigin() {
    if (!m_previewPosition ||
        !PlacedJobPlanning::validPosition(*m_previewPosition, m_deviceProfile)) {
        notify("Benutzerursprung benötigt einen ausdrücklich bekannten Vorschaupositionsstand.");
        return false;
    }
    const bool intentChanged = !m_placementIntent.userOrigin ||
                               m_placementIntent.userOrigin->x() != m_previewPosition->point.x() ||
                               m_placementIntent.userOrigin->y() != m_previewPosition->point.y();
    if (intentChanged)
        checkpoint("Benutzerursprung erfassen");
    m_placementIntent.userOrigin = m_previewPosition->point;
    m_confirmedUserOrigin = m_previewPosition;
    if (intentChanged)
        refresh();
    else {
        refreshPlanning();
        emit changed();
    }
    return true;
}
bool Document::confirmUserOrigin() {
    if (!m_previewPosition || !m_placementIntent.userOrigin) {
        notify("Gespeicherten Benutzerursprung und bekannten aktuellen Vorschaupositionsstand "
               "zuerst bereitstellen.");
        return false;
    }
    auto origin = *m_previewPosition;
    origin.point = *m_placementIntent.userOrigin;
    if (!PlacedJobPlanning::validPosition(origin, m_deviceProfile)) {
        notify("Gespeicherter Benutzerursprung liegt außerhalb des aktuellen Profil-Arbeitsraums.");
        return false;
    }
    m_confirmedUserOrigin = origin;
    refreshPlanning();
    emit changed();
    return true;
}
void Document::clearUserOrigin() {
    if (!m_placementIntent.userOrigin && !m_confirmedUserOrigin)
        return;
    if (m_placementIntent.userOrigin)
        checkpoint("Benutzerursprung entfernen");
    m_placementIntent.userOrigin.reset();
    m_confirmedUserOrigin.reset();
    refresh();
}
QVariantMap Document::placementSummary() const {
    const auto &plan = job();
    auto summary = m_background ? m_preparation.placementSummary() : m_syncPlacementSummary;
    summary["cutSelectedGraphics"] = cutSelectedGraphics();
    summary["useSelectionOrigin"] = useSelectionOrigin();
    summary["selectionOriginActive"] =
        cutSelectedGraphics() && useSelectionOrigin() && startMode() != AbsoluteCoordinates;
    summary["mode"] = int(startMode());
    summary["anchor"] = int(jobOrigin());
    summary["valid"] = plan.valid();
    summary["busy"] = planningBusy();
    if (!plan.valid())
        summary["reason"] = plan.errors.join("; ");
    summary["previewPositionKnown"] = m_previewPosition.has_value();
    summary["originConfirmed"] = m_confirmedUserOrigin.has_value();
    if (m_placementIntent.userOrigin)
        summary["userOrigin"] = QVariantMap{{"present", true},
                                            {"x", m_placementIntent.userOrigin->x()},
                                            {"y", m_placementIntent.userOrigin->y()}};
    else
        summary["userOrigin"] = QVariantMap{{"present", false}};
    return summary;
}

QByteArray Document::offlineRegistrationFrame() const {
    return QCryptographicHash::hash(QByteArrayLiteral("offline-g54-intent-v1:") +
        m_framingInstance.toUtf8() + QByteArray::number(m_registrationFrameRevision) +
        m_deviceProfile.outputFingerprint(), QCryptographicHash::Sha256).toHex();
}
QVariantMap Document::manualRegistrationSummary() const {
    if (!m_manualRegistration)
        return {{"enabled",false},{"valid",false},{"reason","Keine manuelle Ausgaberegistrierung aktiv."}};
    auto summary = placementSummary().value("registration").toMap();
    summary["enabled"] = true;
    summary["busy"] = planningBusy();
    if (summary.isEmpty() || !job().valid()) {
        summary["valid"] = false;
        summary["reason"] = job().errors.join("; ");
    }
    return summary;
}
bool Document::applyManualRegistration(const ManualRegistration::Intent &intent) {
    if (!permitSynchronousWork()) return false;
    if (planningBusy() || batchBusy() || imageExportBusy()) {
        notify("Registrierung während Berechnung oder Export gesperrt."); return false;
    }
    const auto input = jobInput();
    const auto plan = JobPlanner::build(input);
    const auto checked = ManualRegistration::apply(input, plan, intent, offlineRegistrationFrame(), m_previewPosition);
    if (!checked.valid()) { notify(checked.summary.value("reason").toString()); return false; }
    // Registration affects output only. Selecting Absolute uses the existing undoable
    // project intent; the measured/typed alignment itself is intentionally session-only.
    if (startMode() != AbsoluteCoordinates) {
        checkpoint("Absolute Ausgabe für Print and Cut");
        m_placementIntent.mode = JobPlacement::Mode::Absolute;
    }
    m_manualRegistration = intent;
    const QPointer<Document> alive(this);
    refresh();
    if (!alive) return true;
    notify("Manuelle Ausgaberegistrierung aktiv: Simulation, GCode und Auftragsrahmen verwenden die registrierten Wege. Registrierung wird nicht im Projekt gespeichert.");
    return true;
}
void Document::resetManualRegistration() {
    if (!m_manualRegistration) return;
    if (!permitSynchronousWork()) return;
    m_manualRegistration.reset();
    const QPointer<Document> alive(this);
    refreshPlanning();
    if (!alive) return;
    emit changed();
    if (!alive) return;
    notify("Manuelle Ausgaberegistrierung ausdrücklich zurückgesetzt; Startmodus und Ursprung jetzt separat prüfen.");
}
