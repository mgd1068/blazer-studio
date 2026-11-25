#include "preparedcontrollerjob.h"
#include "job.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <QPainter>
#include <QLineF>

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
    auto view=std::make_shared<Visualization>();
    view->workArea=profile.workArea();
    view->sourceFingerprint=result->m_source;view->moveCount=result->m_moveCount;
    view->estimatedSeconds=std::isfinite(plan.seconds)&&plan.seconds>0?plan.seconds:0;
    // One bounded bitmap contains ALL planned paths. Neither the realtime
    // worker nor the GUI needs to rescan up to 500k moves on a status update.
    const double extent=std::max({right-left,top-bottom,1.});
    const double pad=std::max(.01,extent/512.);
    view->workBounds=QRectF(QPointF(left-pad,bottom-pad),QPointF(right+pad,top+pad));
    const double pixels=1020./std::max(view->workBounds.width(),view->workBounds.height());
    const int width=std::clamp(int(std::ceil(view->workBounds.width()*pixels)),2,1024);
    const int height=std::clamp(int(std::ceil(view->workBounds.height()*pixels)),2,1024);
    view->image=QImage(width,height,QImage::Format_ARGB32_Premultiplied);
    view->image.fill(Qt::transparent);
    QPainter painter(&view->image);painter.setRenderHint(QPainter::Antialiasing);
    const auto pixel=[&](QPointF p){return QPointF((p.x()-view->workBounds.left())/view->workBounds.width()*(width-1),
        (view->workBounds.bottom()-p.y())/view->workBounds.height()*(height-1));};
    QPointF cursor=result->m_firstApproach;bool first=true;
    double fallbackSeconds=0;
    for(const auto &move:plan.moves) {
        if(isCancelled(cancel)){painter.end();return fail("Auftragsvorbereitung abgebrochen.",true);}
        const QPen travel(QColor("#8da4b1"),2.5,Qt::DashLine);
        if(move.rapid) {
            painter.setPen(travel);
            // The initial rapid's 'from' is only a preview hint, not emitted.
            if(!first)painter.drawLine(pixel(cursor),pixel(move.to));
        } else {
            if(cursor!=move.from){painter.setPen(travel);painter.drawLine(pixel(cursor),pixel(move.from));}
            painter.setPen(QPen(move.power>0?QColor("#28a4c8"):QColor("#8da4b1"),4.));
            painter.drawLine(pixel(move.from),pixel(move.to));
            if(std::isfinite(move.speed)&&move.speed>0)fallbackSeconds+=QLineF(move.from,move.to).length()/move.speed*60.;
        }
        cursor=move.to;first=false;
    }
    painter.end();
    if(view->estimatedSeconds<=0 && std::isfinite(fallbackSeconds))view->estimatedSeconds=fallbackSeconds;
    result->m_visualization=std::move(view);
    if (isCancelled(cancel)) return fail("Auftragsvorbereitung abgebrochen.", true);
    return result;
}
