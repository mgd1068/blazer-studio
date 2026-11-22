#include "preparedlocaljob.h"
#include "grblwirenumbers.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
using Distances = ManualTravelCheck::Distances;
bool same(QPointF a, QPointF b) {
    return a.x() == b.x() && a.y() == b.y();
}
void include(Distances &d, QPointF p) {
    d.negativeX = std::max(d.negativeX, -p.x());
    d.positiveX = std::max(d.positiveX, p.x());
    d.negativeY = std::max(d.negativeY, -p.y());
    d.positiveY = std::max(d.positiveY, p.y());
}
Distances united(Distances a, Distances b) {
    return {std::max(a.negativeX, b.negativeX), std::max(a.positiveX, b.positiveX),
            std::max(a.negativeY, b.negativeY), std::max(a.positiveY, b.positiveY)};
}
struct Bounds {
    double minX = std::numeric_limits<double>::infinity(), minY = minX;
    double maxX = -minX, maxY = -minX;
    void add(QPointF p) {
        minX = std::min(minX, p.x());
        minY = std::min(minY, p.y());
        maxX = std::max(maxX, p.x());
        maxY = std::max(maxY, p.y());
    }
    QRectF rect() const { return {QPointF(minX, minY), QPointF(maxX, maxY)}; }
};
void setup(QDataStream &s) {
    s.setVersion(QDataStream::Qt_6_0);
    s.setByteOrder(QDataStream::BigEndian);
    s.setFloatingPointPrecision(QDataStream::DoublePrecision);
}
QByteArray hash(const QByteArray &b) {
    return QCryptographicHash::hash(b, QCryptographicHash::Sha256);
}
QByteArray frameDigest(QPointF head, Distances d) {
    QByteArray b;
    QDataStream s(&b, QIODevice::WriteOnly);
    setup(s);
    s << QByteArray("local-job-measurement-data-v1") << head.x() << head.y() << d.negativeX
      << d.positiveX << d.negativeY << d.positiveY << PreparedLocalJob::safetyMarginMm;
    return hash(b);
}
QStringList exceeded(Distances required, Distances usable) {
    QStringList errors;
    const auto check = [&](QString direction, double need, double have) {
        if (need > have)
            errors.append(direction + ": benötigt " + QString::number(need, 'g', 16) +
                          " mm, nach 2 mm Reserve verfügbar " + QString::number(have, 'g', 16) +
                          " mm");
    };
    check("−X", required.negativeX, usable.negativeX);
    check("+X", required.positiveX, usable.positiveX);
    check("−Y", required.negativeY, usable.negativeY);
    check("+Y", required.positiveY, usable.positiveY);
    return errors;
}
} // namespace

bool PreparedLocalJob::inspectWire(const QStringList &lines, double powerScale, const QSizeF &bed,
                                   const Cancellation &cancel) {
    const auto fail = [&](QString error) {
        m_errors = {std::move(error)};
        return false;
    };
    const QStringList prefix = {"M5",
                                "(Blazer Studio: GRBL laser mode, $30=" +
                                    QString::number(powerScale, 'f', 0) + ", $32=1, G54)",
                                "G21",
                                "G90",
                                "G54",
                                "G17",
                                "G94",
                                "S0"};
    if (lines.size() < 10 || lines.size() > 3 * qsizetype(JobPlan::maximumMoves) + 16 ||
        !std::isfinite(powerScale) || powerScale < 1 || powerScale > 1000000000 ||
        std::floor(powerScale) != powerScale || !std::isfinite(bed.width()) ||
        !std::isfinite(bed.height()) || bed.width() <= 0 || bed.height() <= 0 ||
        !std::isfinite(m_head.x()) || !std::isfinite(m_head.y()))
        return fail("Lokale Ausgabe benötigt den bekannten begrenzten GRBL-XY-Vertrag.");
    for (qsizetype i = 0; i < prefix.size(); ++i)
        if (lines[i] != prefix[i])
            return fail("Unbekannter Einheiten-/Koordinatenvertrag der lokalen Ausgabe.");
    // This is deliberately a narrow parser for the CURRENT encoder grammar, not
    // an arbitrary imported GCode interpreter. Every word must be accounted for.
    static const QRegularExpression move(QStringLiteral(
        "^G([01]) X(-?(?:0|[1-9][0-9]*)\\.[0-9]{3}) Y(-?(?:0|[1-9][0-9]*)\\.[0-9]{3})"
        "(?: S((?:0|[1-9][0-9]*)))?(?: F((?:0|[1-9][0-9]*)\\.[0-9]{3}))?$"));
    static const QRegularExpression power(QStringLiteral("^M4 S(0|[1-9][0-9]*)$"));
    static const QRegularExpression comment(
        QStringLiteral("^\\(Relief (?:slice|cleanup after) [1-9][0-9]*\\)$"));
    QPointF previous = m_head;
    Bounds absolute, local;
    local.add({0, 0});
    bool emitting = false, haveFeed = false, haveApproach = false, terminalOff = false;
    m_wire = {};
    m_firstNonzero = -1;
    m_motionLines = 0;
    for (qsizetype i = prefix.size(); i < lines.size(); ++i) {
        if (isCancelled(cancel)) {
            m_cancelled = true;
            return fail("Lokale Vorbereitung abgebrochen.");
        }
        const auto &line = lines[i];
        if (line.isEmpty() || line.toLatin1().size() + 1 > 79 || line.toLatin1() != line.toUtf8())
            return fail("Ungültige oder zu lange lokale GRBL-Ausgabezeile.");
        if (i == lines.size() - 1) {
            if (line != "G0" || !terminalOff || !haveApproach)
                return fail("Unbekanntes Ende der lokalen GRBL-Ausgabe.");
            continue; // Bare final G0 changes modal state; it is NOT a target.
        }
        if (line == "M5" || line == "M5 S0") {
            if (!haveApproach)
                return fail("Lokaler Ausgabepfad hat keine bekannte erste Anfahrt.");
            emitting = false;
            terminalOff = (line == "M5 S0" && i == lines.size() - 2);
            continue;
        }
        if (comment.match(line).hasMatch())
            continue;
        const auto p = power.match(line);
        if (p.hasMatch()) {
            bool ok = false;
            const double value = p.captured(1).toDouble(&ok);
            if (!haveApproach || !ok || !std::isfinite(value) || value > powerScale)
                return fail("Unbekannte Leistungsfreigabe vor der lokalen Anfahrt.");
            emitting = true;
            continue;
        }
        const auto target = move.match(line);
        if (!target.hasMatch())
            return fail("Nicht unterstützter Modus, Ziel oder Achswort in lokaler Ausgabe.");
        const bool rapid = target.captured(1) == "0";
        // The fixed prefix requires laser mode and only M4 is accepted above.
        // Dynamic G0 inhibits output without clearing M4 or retained S; axes,
        // units, coordinate selection and G0's no-S/no-F grammar stay strict.
        if ((rapid &&
             (!target.captured(4).isEmpty() || !target.captured(5).isEmpty())) ||
            (!rapid && (!emitting || !haveApproach)))
            return fail("Unbekannter Laser-/Anfahrtszustand der lokalen Ausgabe.");
        bool xOk = false, yOk = false;
        const QPointF point(target.captured(2).toDouble(&xOk), target.captured(3).toDouble(&yOk));
        if (!xOk || !yOk || !std::isfinite(point.x()) || !std::isfinite(point.y()) ||
            point.x() < 0 || point.y() < 0 || point.x() > bed.width() || point.y() > bed.height())
            return fail("Tatsächliches GRBL-Ziel außerhalb des unveränderten Ausgabeprofils.");
        if (!target.captured(4).isEmpty()) {
            bool ok = false;
            const double value = target.captured(4).toDouble(&ok);
            if (!ok || !std::isfinite(value) || value > powerScale)
                return fail("Nicht darstellbare Leistung in lokaler Ausgabe.");
        }
        if (!target.captured(5).isEmpty()) {
            bool ok = false;
            const double value = target.captured(5).toDouble(&ok);
            if (!ok || !std::isfinite(value) || value < 1 || value > 100000)
                return fail("Nicht darstellbarer Vorschub in lokaler Ausgabe.");
            haveFeed = true;
        }
        if (!rapid && !haveFeed)
            return fail("Lokale G1-Ausgabe ohne bekannten Vorschub.");
        if (!haveApproach) {
            m_wireFirst = point;
            haveApproach = true;
        }
        if (m_firstNonzero < 0 && !same(point, previous))
            m_firstNonzero = i;
        previous = point;
        // Crucial: parse the ACTUAL ABS target, subtract the confirmed raw G54
        // head once. Never round the difference or round the head again.
        const auto displacement = point - m_head;
        if (std::abs(displacement.x()) > ManualTravelCheck::maximumDistanceMm ||
            std::abs(displacement.y()) > ManualTravelCheck::maximumDistanceMm)
            return fail("Tatsächlicher lokaler Wireweg überschreitet den Prüfvertrag.");
        include(m_wire, displacement);
        absolute.add(point);
        local.add(displacement);
        ++m_motionLines;
        if (m_motionLines > JobPlan::maximumMoves + 1)
            return fail("Zu viele tatsächliche lokale Bewegungsziele.");
    }
    m_absoluteWireBounds = absolute.rect();
    m_localWireBounds = local.rect();
    return true;
}

void PreparedLocalJob::prepareFrame(const QSizeF &bed, const Cancellation &cancel) {
    // Axis-aligned straight frame segments have extrema at their endpoints,
    // including the first head-to-corner approach and final return.
    const auto b = m_absoluteWireBounds;
    const QList<QPointF> targets = {b.topLeft(),    b.topRight(), b.bottomRight(),
                                    b.bottomLeft(), b.topLeft(),  m_head};
    QStringList lines = {"M5 S0", "G21", "G90", "G54"};
    m_frameRequired = m_union;
    for (const auto target : targets) {
        if (isCancelled(cancel)) {
            m_cancelled = true;
            m_frameError = "Lokale Rahmenvorbereitung abgebrochen.";
            return;
        }
        const auto x = GrblWireNumbers::format(target.x());
        const auto y = GrblWireNumbers::format(target.y());
        bool xOk = false, yOk = false;
        const QPointF actual(x.toDouble(&xOk), y.toDouble(&yOk));
        if (!xOk || !yOk || !std::isfinite(actual.x()) || !std::isfinite(actual.y()) ||
            actual.x() < 0 || actual.y() < 0 || actual.x() > bed.width() ||
            actual.y() > bed.height()) {
            m_frameError = "Tatsächliches Rahmenziel außerhalb des unveränderten Ausgabeprofils.";
            return;
        }
        const auto rawLocal = target - m_head;
        include(m_frameRequired, rawLocal);
        include(m_frameRequired, {GrblWireNumbers::coordinate(rawLocal.x()),
                                  GrblWireNumbers::coordinate(rawLocal.y())});
        // Return to a fractional confirmed head can differ after output
        // formatting. This residual is real requested travel, never ignored.
        include(m_frameRequired, actual - m_head);
        const auto line = "G1 X" + x + " Y" + y + " S0 F600.000";
        if (line.toLatin1().size() + 1 > 79) {
            m_frameError = "Lokale Rahmenzeile überschreitet den GRBL-Zeilenpuffer.";
            return;
        }
        lines.append(line);
    }
    const auto errors = exceeded(m_frameRequired, m_usable);
    if (!errors.isEmpty()) {
        m_frameError = "Rahmen einschließlich Rückkehr passt nicht: " + errors.join("; ");
        return;
    }
    lines.append("M5 S0");
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    setup(stream);
    stream << QByteArray("prepared-local-frame-program-v1") << m_fingerprint << m_request << m_frame
           << lines;
    if (isCancelled(cancel)) {
        m_cancelled = true;
        m_frameError = "Lokale Rahmenvorbereitung abgebrochen.";
        return;
    }
    m_frameProgramFingerprint = hash(bytes);
    m_frameLines = std::move(lines);
}

PreparedLocalJob::Ptr PreparedLocalJob::prepare(const JobPlan &plan,
                                                const ManualTravelCheck::Request &request,
                                                const Cancellation &cancel) {
    auto result = std::shared_ptr<PreparedLocalJob>(new PreparedLocalJob);
    const auto fail = [&](QStringList errors) -> Ptr {
        result->m_prepared.reset();
        result->m_frameLines.clear();
        result->m_frameProgramFingerprint.clear();
        result->m_frameError = "Kein gültiger lokaler Auftrag für eine Rahmenvorbereitung.";
        result->m_errors = std::move(errors);
        result->m_cancelled = isCancelled(cancel) || result->m_cancelled;
        return result;
    };
    if (isCancelled(cancel))
        return fail({"Lokale Vorbereitung abgebrochen."});
    if (request.safetyMarginMm != safetyMarginMm)
        return fail({"Manueller Kopfstart benötigt ausdrücklich genau 2 mm Reserve je Richtung."});
    result->m_pure = ManualTravelCheck::check(plan, request, cancel);
    if (!result->m_pure.hasRequirements())
        return fail({result->m_pure.error()});
    // A completed pure check can report an insufficiency. Still inspect actual
    // output to provide the full conservative footprint, but publish NO cursor.
    result->m_head = *request.headAnchor;
    result->m_available = result->m_pure.available();
    result->m_usable = result->m_pure.usableAfterMargin();
    result->m_manualSource = result->m_pure.sourceFingerprint();
    result->m_request = result->m_pure.requestFingerprint();
    result->m_frame = frameDigest(result->m_head, result->m_available);
    Bounds raw;
    raw.add({0, 0});
    for (const auto &move : plan.moves) {
        if (isCancelled(cancel))
            return fail({"Lokale Vorbereitung abgebrochen."});
        for (const auto p : {move.from, move.to}) {
            const auto local = p - result->m_head;
            include(result->m_raw, local);
            raw.add(local);
        }
    }
    result->m_rawLocalBounds = raw.rect();
    const auto prepared = PreparedControllerJob::prepare(plan, cancel);
    if (!prepared->valid()) {
        result->m_cancelled = prepared->cancelled();
        return fail(prepared->errors());
    }
    if (prepared->driverId() != "grbl-serial" || prepared->transport() != "serial" ||
        prepared->mediaType() != "text/x-gcode" ||
        prepared->sourceFingerprint() != plan.motionFingerprint(cancel) ||
        prepared->profileFingerprint() != plan.deviceProfile().outputFingerprint())
        return fail({"Lokale Ausgabe gehört zu einem anderen Quellplan oder Ausgabeprofil."});
    result->m_source = prepared->sourceFingerprint();
    result->m_profile = prepared->profileFingerprint();
    result->m_rawFirst = prepared->firstApproach();
    if (!result->inspectWire(prepared->lines(),
                             plan.deviceProfile().driverSettings()["powerScale"].toDouble(),
                             plan.deviceProfile().workArea(), cancel))
        return fail(result->m_errors);
    result->m_union = united(united(result->m_raw, result->m_wire), result->m_pure.required());
    const auto errors = exceeded(result->m_union, result->m_usable);
    if (!errors.isEmpty())
        return fail(errors);
    if (!result->m_pure.valid())
        return fail({result->m_pure.error()});
    if (isCancelled(cancel))
        return fail({"Lokale Vorbereitung abgebrochen."});
    QByteArray bytes;
    QDataStream s(&bytes, QIODevice::WriteOnly);
    setup(s);
    s << QByteArray("prepared-local-job-v1") << result->m_source << result->m_profile
      << result->m_manualSource << result->m_request << result->m_frame << prepared->lines();
    result->m_fingerprint = hash(bytes);
    result->prepareFrame(plan.deviceProfile().workArea(), cancel);
    if (result->m_cancelled || isCancelled(cancel))
        return fail({"Lokale Vorbereitung abgebrochen."});
    result->m_prepared = prepared;
    return result;
}
bool PreparedLocalJob::matches(const JobPlan &plan, const ManualTravelCheck::Request &request,
                               const Cancellation &cancel) const {
    return valid() && request.safetyMarginMm == safetyMarginMm &&
           m_pure.matches(plan, request, cancel) && !isCancelled(cancel);
}
