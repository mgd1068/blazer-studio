#include "manualregistrationcontroller.h"
#include "document.h"
#include "machine.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QPainter>
#include <QUuid>
#include <cmath>
namespace {
QString previewImage(const JobPlan &plan, const ManualRegistration::Parameters &parameters) {
    QImage image(640, 400, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    const auto bed = plan.deviceProfile().workArea();
    const double scale = std::min(600. / bed.width(), 360. / bed.height());
    const QPointF offset((640 - bed.width() * scale) / 2, (400 - bed.height() * scale) / 2);
    auto map = [&](QPointF point) {
        return QPointF(offset.x() + point.x() * scale, 400 - offset.y() - point.y() * scale);
    };
    painter.setPen(QPen(QColor("#708290"), 1));
    painter.drawRect(QRectF(map({0, bed.height()}), map({bed.width(), 0})));
    for (const auto &move : plan.moves) {
        painter.setPen(QPen(move.rapid       ? QColor("#c7d0d5")
                            : move.power > 0 ? QColor("#007c95")
                                             : QColor("#d18a29"),
                            1));
        if (move.rapid && &move == plan.moves.constData())
            continue; // Unknown first.from is not a measured head.
        painter.drawLine(map(move.from), map(move.to));
    }
    painter.setPen(QPen(QColor("#bc3152"), 2));
    for (const auto point : {parameters.targetFirst, parameters.targetSecond}) {
        const auto p = map(point);
        painter.drawLine(p + QPointF(-5, 0), p + QPointF(5, 0));
        painter.drawLine(p + QPointF(0, -5), p + QPointF(0, 5));
    }
    painter.end();
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG") || png.size() > 1024 * 1024)
        return {};
    return "data:image/png;base64," + QString::fromLatin1(png.toBase64());
}
bool positionEqual(const std::optional<JobPlacement::PositionSnapshot> &a,
                   const std::optional<JobPlacement::PositionSnapshot> &b) {
    if (a.has_value() != b.has_value())
        return false;
    return !a ||
           (a->point.x() == b->point.x() && a->point.y() == b->point.y() && a->known == b->known &&
            a->revision == b->revision && a->profileFingerprint == b->profileFingerprint &&
            a->coordinateFrameFingerprint == b->coordinateFrameFingerprint);
}
} // namespace
ManualRegistrationController::ManualRegistrationController(Document *document, Machine *machine,
                                                           QObject *parent)
    : QObject(parent), m_document(document), m_machine(machine) {
    if (document) {
        connect(document, &Document::changed, this, &ManualRegistrationController::sourceChanged);
        connect(document, &Document::selectionChanged, this,
                &ManualRegistrationController::sourceChanged);
        connect(document, &Document::synchronousWorkGuardChanged, this,
                &ManualRegistrationController::changed);
        connect(document, &QObject::destroyed, this, &ManualRegistrationController::sourceChanged);
    }
    if (machine) {
        connect(machine, &Simulator::changed, this, &ManualRegistrationController::changed);
        connect(machine, &QObject::destroyed, this, &ManualRegistrationController::changed);
    }
}
QVariantMap ManualRegistrationController::state() const {
    return m_document ? m_document->manualRegistrationSummary()
                      : QVariantMap{{"enabled", false}, {"reason", "Dokument nicht verfügbar."}};
}
QString ManualRegistrationController::blockReason() const {
    if (!m_document || !m_machine)
        return "Dokument oder Simulator nicht verfügbar.";
    if (const auto reason = m_document->synchronousWorkBlockReason(); !reason.isEmpty())
        return reason;
    if (m_machine->occupied() || m_machine->requestPending() || m_machine->busy() ||
        m_machine->firing())
        return "Registrierung während Geräteaktion, Simulation oder Fokus gesperrt. Ablauf zuerst "
               "beenden.";
    if (m_document->planningBusy() || m_document->batchBusy() || m_document->imageExportBusy())
        return "Berechnung oder Export läuft. Registrierung anschließend erneut prüfen.";
    return {};
}
bool ManualRegistrationController::fail(QString message) {
    m_message = std::move(message);
    emit changed();
    return false;
}
bool ManualRegistrationController::sourceStillCurrent() const {
    // Read-only COW identities make generic GUI notifications cheap. Authoritative
    // commit/planning still verifies the exact content digest against the frozen input.
    if (!m_document || m_frame != m_document->offlineRegistrationFrame() ||
        m_document->items().constData() != m_sourceItems.constData() ||
        m_document->layers().constData() != m_sourceLayers.constData() ||
        m_document->activePage() != m_sourcePage ||
        m_document->cutSelectedGraphics() != m_sourceCutSelected ||
        !positionEqual(m_position, m_document->previewPositionSnapshot()))
        return false;
    if (m_sourceCutSelected) {
        QStringList selected;
        for (const auto &row : m_document->selectedRows()) {
            const int index = row.toInt();
            if (index >= 0 && index < m_document->items().size())
                selected.append(m_document->items()[index].id);
        }
        selected.sort();
        return selected == m_sourceSelection;
    }
    return true;
}
void ManualRegistrationController::sourceChanged() {
    const bool busy =
        m_document && (!m_document->synchronousWorkBlockReason().isEmpty() ||
                       (m_machine && (m_machine->occupied() || m_machine->requestPending() ||
                                      m_machine->busy() || m_machine->firing())));
    if ((!m_review.isEmpty() || m_intent) && (busy || !sourceStillCurrent())) {
        m_intent.reset();
        m_token.clear();
        m_review.clear();
        m_message = "Quellstand oder Anfahrt hat sich geändert. Referenzpunkte erneut prüfen; alte "
                    "Zustimmung ist ungültig.";
    }
    emit changed();
}
void ManualRegistrationController::clearReview() {
    m_intent.reset();
    m_token.clear();
    m_review.clear();
    emit changed();
}
QVariantMap ManualRegistrationController::reviewPoints(const QVariantList &values,
                                                       bool proportional,
                                                       double maximumResidualMm) {
    m_intent.reset();
    m_token.clear();
    m_review.clear();
    auto invalid = [&](QString reason) {
        m_message = reason;
        m_review = {{"valid", false}, {"reason", reason}};
        const auto result = m_review;
        emit changed();
        return result;
    };
    if (const auto reason = blockReason(); !reason.isEmpty())
        return invalid(reason);
    if (values.size() != 8)
        return invalid("Genau zwei Design-/Zielpunktpaare in Millimetern eingeben.");
    QList<double> coordinates;
    for (const auto &value : values) {
        if (value.typeId() != QMetaType::Double && value.typeId() != QMetaType::Int &&
            value.typeId() != QMetaType::Float && value.typeId() != QMetaType::LongLong)
            return invalid(
                "Referenzkoordinaten müssen numerisch sein; kein Text oder boolescher Ersatzwert.");
        const double coordinate = value.toDouble();
        if (!std::isfinite(coordinate))
            return invalid("Referenzkoordinaten müssen endlich sein.");
        coordinates.append(coordinate);
    }
    m_sourceItems = m_document->items();
    m_sourceLayers = m_document->layers();
    m_sourcePage = m_document->activePage();
    m_sourceCutSelected = m_document->cutSelectedGraphics();
    const auto input = m_document->jobInput();
    m_sourceSelection = input.selectedIds;
    m_sourceSelection.sort();
    const auto canonical = JobPlanner::build(input);
    m_frame = m_document->offlineRegistrationFrame();
    m_geometry = ManualRegistration::geometryFingerprint(input);
    m_position = m_document->previewPositionSnapshot();
    ManualRegistration::Intent intent;
    intent.source = ManualRegistration::SourceSnapshot::capture(input, canonical, m_frame);
    intent.parameters = {{coordinates[0], coordinates[1]},
                         {coordinates[4], coordinates[5]},
                         {coordinates[2], coordinates[3]},
                         {coordinates[6], coordinates[7]},
                         proportional ? ManualRegistration::Scaling::Uniform
                                      : ManualRegistration::Scaling::Rigid,
                         maximumResidualMm};
    const auto result = ManualRegistration::apply(input, canonical, intent, m_frame, m_position);
    if (!result.valid())
        return invalid(result.summary.value("reason").toString());
    m_intent = intent;
    m_token = QString::fromLatin1(
        QCryptographicHash::hash(intent.source.fingerprint() + QUuid::createUuid().toRfc4122(),
                                 QCryptographicHash::Sha256)
            .toHex());
    m_review = result.summary;
    m_review["token"] = m_token;
    m_review["previewSource"] = previewImage(result.plan, intent.parameters);
    m_message = "Registrierte Ausgabe geprüft. Zum Übernehmen ausdrücklich bestätigen; keine reale "
                "Punktmessung erfolgt.";
    const auto answer = m_review;
    emit changed();
    return answer;
}
bool ManualRegistrationController::applyReview(const QString &expectedToken) {
    if (const auto reason = blockReason(); !reason.isEmpty())
        return fail(reason);
    if (!m_intent || m_token.isEmpty() || expectedToken != m_token || !sourceStillCurrent())
        return fail("Registrierungsprüfung ist veraltet oder gehört zu einem anderen Quellstand. "
                    "Erneut prüfen.");
    const auto intent = *m_intent;
    m_intent.reset();
    m_token.clear();
    m_review["token"] = QString(); // one explicit, non-replayable consent
    const QPointer<ManualRegistrationController> alive(this);
    const bool accepted = m_document->applyManualRegistration(intent);
    if (!alive)
        return accepted;
    m_message =
        accepted ? "Registrierte Ausgabe aktiv. Projektdateien speichern diese Ausrichtung nicht."
        : m_document ? m_document->message()
                     : "Dokument nicht verfügbar.";
    emit changed();
    return accepted;
}
bool ManualRegistrationController::resetAlignment() {
    if (const auto reason = blockReason(); !reason.isEmpty())
        return fail(reason);
    m_intent.reset();
    m_token.clear();
    m_review.clear();
    const QPointer<ManualRegistrationController> alive(this);
    m_document->resetManualRegistration();
    if (!alive)
        return true;
    m_message = "Ausgaberegistrierung zurückgesetzt.";
    emit changed();
    return true;
}
QVariantMap ManualRegistrationController::selectionReference() const {
    auto invalid = [](QString reason) { return QVariantMap{{"valid", false}, {"reason", reason}}; };
    if (!m_document)
        return invalid("Dokument nicht verfügbar.");
    const auto rows = m_document->selectedRows();
    if (rows.isEmpty())
        return invalid("Eine sichtbare Vektormarke oder vollständige Gruppe auswählen.");
    const auto &items = m_document->items();
    QString group;
    QRectF bounds;
    bool first = true;
    for (const auto &value : rows) {
        const int row = value.toInt();
        if (row < 0 || row >= items.size())
            return invalid("Auswahl ist veraltet.");
        const auto &art = items[row];
        if (!art.visible || art.page != m_document->activePage() ||
            !QStringList{"rect", "ellipse", "path", "text", "svg"}.contains(art.kind))
            return invalid("Referenz benötigt sichtbare Vektorgeometrie auf diesem Blatt; keine "
                           "Bild-/Tiefenkarte.");
        if (first) {
            group = art.groupId;
            bounds = art.bounds;
            first = false;
        } else {
            if (group.isEmpty() || group != art.groupId)
                return invalid(
                    "Mehrere Marken erst zu einer eindeutigen Referenzgruppe zusammenfassen.");
            bounds = bounds.united(art.bounds);
        }
    }
    if (rows.size() > 1) {
        int count = 0;
        for (const auto &art : items)
            if (art.groupId == group && art.page == m_document->activePage())
                ++count;
        if (count != rows.size())
            return invalid("Die Referenzgruppe vollständig auswählen.");
    }
    return {{"valid", true},
            {"x", bounds.center().x()},
            {"y", bounds.center().y()},
            {"objects", rows.size()}};
}
