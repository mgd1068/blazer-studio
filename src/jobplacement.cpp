#include "jobplacement.h"
#include "geometry.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QLineF>
#include <cmath>

namespace {
bool fingerprint(const QByteArray &bytes) {
    if (bytes.size() != 64)
        return false;
    for (const auto c : bytes)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}
bool finite(QPointF point) {
    return std::isfinite(point.x()) && std::isfinite(point.y());
}
bool inside(QPointF point, QSizeF bed) {
    return finite(point) && point.x() >= 0 && point.y() >= 0 && point.x() <= bed.width() &&
           point.y() <= bed.height();
}
bool boundsValid(const QRectF &bounds) {
    return finite(bounds.topLeft()) && finite(bounds.bottomRight()) &&
           std::isfinite(bounds.width()) && std::isfinite(bounds.height()) && bounds.width() >= 0 &&
           bounds.height() >= 0 && bounds.width() <= Geometry::coordinateLimit &&
           bounds.height() <= Geometry::coordinateLimit &&
           std::abs(bounds.left()) <= Geometry::coordinateLimit &&
           std::abs(bounds.right()) <= Geometry::coordinateLimit &&
           std::abs(bounds.top()) <= Geometry::coordinateLimit &&
           std::abs(bounds.bottom()) <= Geometry::coordinateLimit;
}
QString validateMoves(const JobPlan &plan, const Cancellation &cancel) {
    if (!plan.valid() || plan.moves.size() > JobPlan::maximumMoves)
        return "Platzierung benötigt einen gültigen begrenzten Bewegungsplan.";
    const auto bed = plan.deviceProfile().workArea();
    if (!std::isfinite(bed.width()) || !std::isfinite(bed.height()) || bed.width() <= 0 ||
        bed.height() <= 0 || bed.width() > Geometry::coordinateLimit ||
        bed.height() > Geometry::coordinateLimit)
        return "Arbeitsraum überschreitet den Platzierungsvertrag (maximal 2000 mm).";
    std::optional<QPointF> previous;
    for (const auto &move : plan.moves) {
        if (isCancelled(cancel))
            return "Platzierung abgebrochen.";
        if (!inside(move.from, bed) || !inside(move.to, bed))
            return "Bewegungsplan einschließlich Anfahrt oder Überlauf außerhalb des Arbeitsraums.";
        if (!std::isfinite(move.speed) || move.speed < 1 || move.speed > 100000 ||
            !std::isfinite(move.power) || move.power < 0 || move.power > 100 ||
            (move.rapid && move.power != 0) || (move.power > 0 && move.from == move.to) ||
            move.layer < 0 || move.layer >= 64)
            return "Ungültiger Vorschub, Leistungswert oder Bewegungsvertrag.";
        if (previous && QLineF(*previous, move.from).length() > 1e-8)
            return "Unterbrochener Bewegungsplan; keine automatische Verbindungsgravur.";
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
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}
void writePosition(QDataStream &stream,
                   const std::optional<JobPlacement::PositionSnapshot> &position) {
    stream << bool(position);
    if (position)
        stream << position->point << position->known << position->profileFingerprint
               << position->coordinateFrameFingerprint << position->revision;
}
QByteArray requestFingerprint(const JobPlacement::Request &request) {
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    setup(stream);
    stream << qint32(1) << request.expectedSourceFingerprint << qint32(request.mode);
    if (request.mode != JobPlacement::Mode::Absolute)
        stream << qint32(request.anchor);
    writePosition(stream, request.startPosition);
    if (request.mode == JobPlacement::Mode::UserOrigin)
        writePosition(stream, request.userOrigin);
    return digest(bytes);
}
bool positionValid(const JobPlacement::PositionSnapshot &position, const DeviceProfile &profile) {
    return position.known && position.revision > 0 &&
           position.profileFingerprint == profile.outputFingerprint() &&
           fingerprint(position.coordinateFrameFingerprint) &&
           inside(position.point, profile.workArea());
}
void metrics(JobPlan &plan) {
    plan.workLength = plan.travelLength = plan.seconds = plan.minimumSeconds = 0;
    for (const auto &move : plan.moves) {
        const double length = QLineF(move.from, move.to).length();
        ((move.rapid || move.power <= 0) ? plan.travelLength : plan.workLength) += length;
        plan.minimumSeconds += length / move.speed * 60;
        const double velocity = move.speed / 60;
        plan.seconds += length >= velocity * velocity / 240 ? length / velocity + velocity / 240
                                                            : 2 * std::sqrt(length / 240);
    }
}
} // namespace

JobPlacement::SourceSnapshot
JobPlacement::SourceSnapshot::capture(const JobPlan &plan, const QRectF &graphicsBounds,
                                      const QByteArray &geometryFingerprint, OriginScope scope,
                                      const Cancellation &cancel) {
    SourceSnapshot result;
    result.m_error = validateMoves(plan, cancel);
    if (!result.m_error.isEmpty())
        return result;
    if (!boundsValid(graphicsBounds) || !::fingerprint(geometryFingerprint) ||
        (scope != OriginScope::Design && scope != OriginScope::Selection)) {
        result.m_error = "Ungültige Grafikgrenzen, Geometriesignatur oder Ursprungsauswahl.";
        return result;
    }
    result.m_motion = plan.motionFingerprint(cancel);
    if (result.m_motion.isEmpty() || isCancelled(cancel)) {
        result.m_error = "Platzierung abgebrochen oder Quellplan nicht bindbar.";
        return result;
    }
    result.m_bounds = graphicsBounds;
    result.m_scope = scope;
    result.m_geometry = geometryFingerprint;
    result.m_profile = plan.deviceProfile().outputFingerprint();
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    setup(stream);
    stream << qint32(1) << result.m_motion << result.m_profile << result.m_geometry
           << graphicsBounds << qint32(scope);
    result.m_fingerprint = digest(bytes);
    return result;
}
bool JobPlacement::SourceSnapshot::matches(const JobPlan &plan,
                                           const QByteArray &geometryFingerprint,
                                           const Cancellation &cancel) const {
    return valid() && !isCancelled(cancel) && m_geometry == geometryFingerprint &&
           m_profile == plan.deviceProfile().outputFingerprint() &&
           m_motion == plan.motionFingerprint(cancel) && !isCancelled(cancel);
}
bool JobPlacement::Result::matches(const JobPlan &plan, const SourceSnapshot &source,
                                   const Request &request, const Cancellation &cancel) const {
    return valid() && source.matches(plan, source.geometryFingerprint(), cancel) &&
           m_source == source.fingerprint() && m_request == ::requestFingerprint(request) &&
           request.expectedSourceFingerprint == m_source;
}
JobPlacement::Result JobPlacement::place(const JobPlan &plan, const SourceSnapshot &source,
                                         const Request &request, const Cancellation &cancel) {
    Result result;
    auto fail = [&](const QString &error) {
        result.m_error = error;
        result.m_plan = JobPlan(plan.deviceProfile());
        return result;
    };
    if (!source.matches(plan, source.geometryFingerprint(), cancel) ||
        request.expectedSourceFingerprint != source.fingerprint())
        return fail(
            "Quellplan, Geräteprofil oder Ursprungsvorschau ist veraltet; erneut berechnen.");
    if (request.mode != Mode::Absolute && request.mode != Mode::CurrentPosition &&
        request.mode != Mode::UserOrigin)
        return fail("Unbekannter Startmodus.");
    if (request.mode != Mode::Absolute && (int(request.anchor) < int(Anchor::LowerLeft) ||
                                           int(request.anchor) > int(Anchor::UpperRight)))
        return fail("Ungültiger Neunpunkt-Ursprung.");
    if (request.startPosition && !positionValid(*request.startPosition, plan.deviceProfile()))
        return fail("Expliziter Anfahrtspunkt ist unbekannt, nicht gebunden oder außerhalb des "
                    "Arbeitsraums.");
    if (request.mode != Mode::Absolute && !request.startPosition)
        return fail("Relativer Start benötigt einen ausdrücklich bekannten Positionsstand.");
    QPointF target;
    if (request.mode == Mode::CurrentPosition)
        target = request.startPosition->point;
    if (request.mode == Mode::UserOrigin) {
        if (!request.userOrigin || !positionValid(*request.userOrigin, plan.deviceProfile()) ||
            request.userOrigin->coordinateFrameFingerprint !=
                request.startPosition->coordinateFrameFingerprint)
            return fail("Benutzerursprung fehlt oder gehört zu einem anderen Koordinatenrahmen.");
        target = request.userOrigin->point;
    }
    if (request.mode != Mode::Absolute) {
        const auto bounds = source.graphicsBounds();
        const int column = int(request.anchor) % 3, row = int(request.anchor) / 3;
        const QPointF anchor(bounds.left() + column * bounds.width() / 2,
                             bounds.top() + row * bounds.height() / 2);
        result.m_translation = target - anchor;
    }
    result.m_plan = plan;
    for (qsizetype i = 0; i < result.m_plan.moves.size(); ++i) {
        if (isCancelled(cancel))
            return fail("Platzierung abgebrochen.");
        auto move = result.m_plan.moves[i];
        move.from += result.m_translation;
        move.to += result.m_translation;
        if (i == 0 && move.rapid && request.startPosition)
            move.from = request.startPosition->point;
        result.m_plan.moves.replace(i, move);
    }
    if (request.startPosition && !result.m_plan.moves.first().rapid) {
        if (result.m_plan.moves.size() >= JobPlan::maximumMoves)
            return fail("Laserfreie Anfahrt würde das Bewegungsbudget überschreiten.");
        JobMoves placed;
        placed.reserve(result.m_plan.moves.size() + 1);
        const auto first = result.m_plan.moves.first();
        placed.append(
            {request.startPosition->point, first.from, 3000, 0, first.layer, true, first.slice});
        for (const auto &move : result.m_plan.moves)
            placed.append(move);
        result.m_plan.moves = std::move(placed);
    }
    const auto error = validateMoves(result.m_plan, cancel);
    if (!error.isEmpty())
        return fail(error);
    metrics(result.m_plan);
    if (isCancelled(cancel))
        return fail("Platzierung abgebrochen.");
    result.m_graphicsBounds = source.graphicsBounds().translated(result.m_translation);
    result.m_explicitApproach = request.startPosition.has_value();
    result.m_source = source.fingerprint();
    result.m_request = ::requestFingerprint(request);
    return result;
}
