#include "manualtravelcheck.h"
#include "grblwirenumbers.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <algorithm>
#include <cmath>

namespace {
bool finitePoint(QPointF p) {
    return std::isfinite(p.x()) && std::isfinite(p.y()) &&
           std::abs(p.x()) <= ManualTravelCheck::maximumDistanceMm &&
           std::abs(p.y()) <= ManualTravelCheck::maximumDistanceMm;
}
bool samePoint(QPointF a, QPointF b) {
    return a.x() == b.x() && a.y() == b.y();
}
QString planError(const JobPlan &plan, const Cancellation &cancel) {
    if (isCancelled(cancel))
        return "Restwegprüfung abgebrochen.";
    if (!plan.valid() || plan.moves.size() > JobPlan::maximumMoves)
        return "Restwegprüfung benötigt einen gültigen Plan mit höchstens 500000 Bewegungen.";
    const auto bed = plan.deviceProfile().workArea();
    if (!std::isfinite(bed.width()) || !std::isfinite(bed.height()) || bed.width() <= 0 ||
        bed.height() <= 0 || bed.width() > ManualTravelCheck::maximumDistanceMm ||
        bed.height() > ManualTravelCheck::maximumDistanceMm)
        return "Ungültiger oder unplausibler Plan-Arbeitsraum.";
    const auto outputError = ControllerAdapters::outputError(plan.deviceProfile());
    if (!outputError.isEmpty())
        return outputError;
    std::optional<QPointF> previous;
    for (const auto &move : plan.moves) {
        if (isCancelled(cancel))
            return "Restwegprüfung abgebrochen.";
        if (!finitePoint(move.from) || !finitePoint(move.to))
            return "Bewegungsplan enthält unbekannte oder unplausible virtuelle Koordinaten.";
        if (!std::isfinite(move.speed) || move.speed < 1 || move.speed > 100000 ||
            !std::isfinite(move.power) || move.power < 0 || move.power > 100 ||
            (move.rapid && move.power != 0) || (move.power > 0 && samePoint(move.from, move.to)) ||
            move.layer < 0 || move.layer >= 64 || move.slice < 0)
            return "Ungültiger Vorschub, Leistungswert oder Bewegungsvertrag.";
        if (previous && !samePoint(*previous, move.from))
            return "Unterbrochener Bewegungsplan; fehlende Anfahrten werden nicht erfunden.";
        previous = move.to;
    }
    return {};
}
void setup(QDataStream &stream) {
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::BigEndian);
    stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
}
QByteArray digest(const QByteArray &bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
QByteArray sourceDigest(const JobPlan &plan, const Cancellation &cancel) {
    const auto motion = plan.motionFingerprint(cancel);
    if (motion.size() != 32 || isCancelled(cancel))
        return {};
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    setup(stream);
    stream << QByteArray("manual-travel-source-v1") << plan.deviceProfile().outputFingerprint()
           << motion;
    return digest(bytes);
}
QString requestError(const ManualTravelCheck::Request &request) {
    if (!request.headAnchor || !finitePoint(*request.headAnchor))
        return "Ein ausdrücklich bekannter virtueller Kopfanker ist erforderlich.";
    if (!request.available)
        return "Alle vier freien Restwege müssen ausdrücklich bekannt sein.";
    const auto d = *request.available;
    for (const double value : {d.negativeX, d.positiveX, d.negativeY, d.positiveY})
        if (!std::isfinite(value) || value < 0 || value > ManualTravelCheck::maximumDistanceMm)
            return "Freie Restwege müssen bekannte plausible Werte von 0 bis 2000 mm sein.";
    if (!std::isfinite(request.safetyMarginMm) || request.safetyMarginMm < 0 ||
        request.safetyMarginMm > ManualTravelCheck::maximumDistanceMm)
        return "Sicherheitsabstand muss ausdrücklich mit 0 bis 2000 mm angegeben werden.";
    if (request.expectedSourceFingerprint.size() != 32)
        return "Erwarteter Quellplan-Fingerabdruck fehlt oder ist ungültig.";
    return {};
}
QByteArray requestDigest(const ManualTravelCheck::Request &request) {
    if (!requestError(request).isEmpty())
        return {};
    const auto d = *request.available;
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    setup(stream);
    stream << QByteArray("manual-travel-request-v1") << request.expectedSourceFingerprint
           << request.headAnchor->x() << request.headAnchor->y() << d.negativeX << d.positiveX
           << d.negativeY << d.positiveY << request.safetyMarginMm;
    return digest(bytes);
}
void includePoint(ManualTravelCheck::Distances &required, QPointF p) {
    required.negativeX = std::max(required.negativeX, -p.x());
    required.positiveX = std::max(required.positiveX, p.x());
    required.negativeY = std::max(required.negativeY, -p.y());
    required.positiveY = std::max(required.positiveY, p.y());
}
QString number(double value) {
    return QString::number(value, 'g', 16);
}
} // namespace

namespace ManualTravelCheck {
QByteArray sourceFingerprint(const JobPlan &plan, const Cancellation &cancel) {
    if (!planError(plan, cancel).isEmpty())
        return {};
    return sourceDigest(plan, cancel);
}
Result check(const JobPlan &plan, const Request &request, const Cancellation &cancel) {
    Result result;
    result.m_error = planError(plan, cancel);
    if (!result.m_error.isEmpty())
        return result;
    result.m_source = sourceDigest(plan, cancel);
    if (result.m_source.isEmpty()) {
        result.m_error = "Quellplan konnte nicht gebunden werden oder wurde abgebrochen.";
        return result;
    }
    result.m_error = requestError(request);
    if (!result.m_error.isEmpty())
        return result;
    if (request.expectedSourceFingerprint != result.m_source) {
        result.m_error = "Restwegprüfung gehört zu einem veralteten oder anderen Quellplan.";
        return result;
    }
    // Conservative plausibility cap only. A nominal profile span does not tell
    // us where the head is or prove that either measured direction is safe.
    const auto free = *request.available;
    const auto nominal = plan.deviceProfile().workArea();
    if (free.negativeX + free.positiveX > nominal.width() ||
        free.negativeY + free.positiveY > nominal.height()) {
        result.m_error = "Summe der freien Gegenrichtungen überschreitet den nominellen "
                         "Profilachsweg; Restwegangaben zuerst prüfen.";
        return result;
    }
    if (!samePoint(plan.moves.first().from, *request.headAnchor)) {
        result.m_error = "Erste Anfahrt beginnt nicht am ausdrücklich angegebenen Kopfanker.";
        return result;
    }
    result.m_request = requestDigest(request);
    result.m_available = *request.available;
    result.m_margin = request.safetyMarginMm;
    result.m_usable = {result.m_available.negativeX - result.m_margin,
                       result.m_available.positiveX - result.m_margin,
                       result.m_available.negativeY - result.m_margin,
                       result.m_available.positiveY - result.m_margin};
    for (const auto &move : plan.moves) {
        if (isCancelled(cancel)) {
            result.m_error = "Restwegprüfung abgebrochen.";
            return result;
        }
        for (const auto point : {move.from, move.to}) {
            const auto local = point - *request.headAnchor;
            if (!finitePoint(local)) {
                result.m_error = "Lokaler Verfahrweg überschreitet den 2000-mm-Prüfvertrag.";
                return result;
            }
            // Absolute local coordinates, independently quantized at each endpoint.
            // No cumulative G91 deltas and no discarded nominal rapid.from.
            const QPointF wire(GrblWireNumbers::coordinate(local.x()),
                               GrblWireNumbers::coordinate(local.y()));
            if (!finitePoint(wire)) {
                result.m_error = "Lokale ABS-Ausgabekoordinate ist nicht darstellbar.";
                return result;
            }
            includePoint(result.m_required, local);
            includePoint(result.m_required, wire);
        }
    }
    QStringList exceeded;
    const auto compare = [&](QString direction, double required, double available) {
        if (required > available)
            exceeded.append(direction + ": benötigt " + number(required) +
                            " mm, nach Sicherheitsabstand verfügbar " + number(available) + " mm");
    };
    compare("−X", result.m_required.negativeX, result.m_usable.negativeX);
    compare("+X", result.m_required.positiveX, result.m_usable.positiveX);
    compare("−Y", result.m_required.negativeY, result.m_usable.negativeY);
    compare("+Y", result.m_required.positiveY, result.m_usable.positiveY);
    if (isCancelled(cancel)) {
        result.m_error = "Restwegprüfung abgebrochen.";
        return result;
    }
    result.m_checked = true;
    result.m_error = exceeded.join("; ");
    return result;
}
bool Result::matches(const JobPlan &plan, const Request &request,
                     const Cancellation &cancel) const {
    return valid() && !isCancelled(cancel) && requestDigest(request) == m_request &&
           request.expectedSourceFingerprint == m_source &&
           ManualTravelCheck::sourceFingerprint(plan, cancel) == m_source && !isCancelled(cancel);
}
} // namespace ManualTravelCheck
