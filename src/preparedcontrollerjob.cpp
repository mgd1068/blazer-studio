#include "preparedcontrollerjob.h"
#include "job.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

PreparedControllerJob::Ptr PreparedControllerJob::prepare(const JobPlan &plan,
                                                        const Cancellation &cancel) {
    // A mutable construction handle never escapes; published callers receive
    // only const ownership. They must retire large last references outside RT.
    auto result = std::shared_ptr<PreparedControllerJob>(new PreparedControllerJob);
    const auto fail = [&](QString error, bool cancelled = false) -> Ptr {
        result->m_cancelled = cancelled;
        result->m_errors = {std::move(error)};
        result->m_lines.clear();
        return result;
    };
    if (isCancelled(cancel))
        return fail("Auftragsvorbereitung abgebrochen.", true);
    if (!plan.valid() || plan.moves.size() > JobPlan::maximumMoves)
        return fail("Kein gültiger, begrenzter Bewegungsplan für die Vorbereitung.");
    const auto &profile = plan.deviceProfile();
    if (profile.driverId() != "grbl-serial" || profile.transport() != "serial")
        return fail("Vorbereitete Ausgabe benötigt den bekannten GRBL-XY-Vertrag.");
    const auto program = plan.encodedProgram(cancel);
    if (isCancelled(cancel) || program.cancelled())
        return fail("Auftragsvorbereitung abgebrochen.", true);
    if (!program.valid() || program.mediaType() != "text/x-gcode" ||
        !program.matches(profile, program.sourceFingerprint()))
        return fail(program.errors().isEmpty() ? QString("Kein passender GRBL-Ausgabevertrag.")
                                               : program.errors().join("; "));

    double left = std::numeric_limits<double>::infinity(), right = -left;
    double bottom = left, top = -left;
    const auto include = [&](QPointF point) {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y())) return false;
        left = std::min(left, point.x()); right = std::max(right, point.x());
        bottom = std::min(bottom, point.y()); top = std::max(top, point.y());
        return true;
    };
    // A rapid's preview 'from' is never emitted. Every rapid target and every
    // emitting from/to endpoint is emitted or an explicit output-off approach.
    for (const auto &move : plan.moves) {
        if (isCancelled(cancel)) return fail("Auftragsvorbereitung abgebrochen.", true);
        if ((!move.rapid && !include(move.from)) || !include(move.to))
            return fail("Nicht endliche Koordinate im vorbereiteten Bewegungsplan.");
    }
    auto lines = program.gcodeLines();
    if (isCancelled(cancel)) return fail("Auftragsvorbereitung abgebrochen.", true);
    if (lines.isEmpty()) return fail("Leere vorbereitete GRBL-Ausgabe.");
    for (const auto &line : std::as_const(lines)) {
        if (isCancelled(cancel)) return fail("Auftragsvorbereitung abgebrochen.", true);
        // Same 79-byte wire limit as GrblSession::pump, including newline.
        // Reject the whole candidate before any output-off prefix is sent.
        if (line.isEmpty() || line.toLatin1().size()+1 > 79)
            return fail("Vorbereitete GCode-Zeile überschreitet den GRBL-Zeilenpuffer.");
    }
    result->m_bounds = QRectF(QPointF(left, bottom), QPointF(right, top));
    result->m_firstApproach = plan.moves.first().rapid ? plan.moves.first().to
                                                      : plan.moves.first().from;
    result->m_profile = program.profileFingerprint();
    result->m_source = program.sourceFingerprint();
    result->m_driver = program.driverId();
    result->m_transport = program.transport();
    result->m_media = program.mediaType();
    result->m_moveCount = plan.moves.size();
    result->m_lines = std::move(lines);
    if (isCancelled(cancel)) return fail("Auftragsvorbereitung abgebrochen.", true);
    return result;
}
