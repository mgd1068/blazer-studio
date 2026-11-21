#include "manualregistration.h"
#include "geometry.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QLineF>
#include <cmath>
namespace {
bool finite(QPointF p) {
    return std::isfinite(p.x()) && std::isfinite(p.y());
}
bool inside(QPointF p, QSizeF bed) {
    return finite(p) && p.x() >= 0 && p.y() >= 0 && p.x() <= bed.width() && p.y() <= bed.height();
}
bool hash(const QByteArray &value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
QByteArray digest(const QByteArray &value) {
    return QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex();
}
void setup(QDataStream &stream) {
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::BigEndian);
    stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
}
QString validate(const JobPlan &plan, const Cancellation &cancel) {
    if (!plan.valid() || plan.moves.size() > JobPlan::maximumMoves)
        return "Registrierung benötigt einen gültigen begrenzten Quellauftrag.";
    std::optional<QPointF> previous;
    const auto bed = plan.deviceProfile().workArea();
    if (!std::isfinite(bed.width()) || !std::isfinite(bed.height()) || bed.width() <= 0 ||
        bed.height() <= 0 || bed.width() > Geometry::coordinateLimit ||
        bed.height() > Geometry::coordinateLimit)
        return "Registrierung unterstützt begrenzte Profilflächen bis 2000 × 2000 mm.";
    for (const auto &move : plan.moves) {
        if (isCancelled(cancel))
            return "Registrierung abgebrochen.";
        if (!inside(move.from, bed) || !inside(move.to, bed))
            return "Registrierte Ausgabe einschließlich Anfahrt und Überlauf außerhalb der "
                   "Arbeitsfläche.";
        if (!std::isfinite(move.speed) || move.speed < 1 || move.speed > 100000 ||
            !std::isfinite(move.power) || move.power < 0 || move.power > 100 ||
            (move.rapid && move.power != 0) ||
            (move.power > 0 && move.from.x() == move.to.x() && move.from.y() == move.to.y()) ||
            move.layer < 0 || move.layer >= 64)
            return "Ungültige Bewegung, Geschwindigkeit oder Leistung im Registrierungsauftrag.";
        if (previous && QLineF(*previous, move.from).length() > 1e-8)
            return "Unterbrochener Ausgabepfad; keine automatische Verbindungsgravur.";
        previous = move.to;
    }
    return {};
}
QVariantMap rectangle(QRectF bounds) {
    return {{"x", bounds.x()},
            {"y", bounds.y()},
            {"width", bounds.width()},
            {"height", bounds.height()}};
}
} // namespace
QByteArray ManualRegistration::geometryFingerprint(const JobInput &input,
                                                   const Cancellation &cancel) {
    if (input.items.size() > Geometry::objectLimit ||
        input.selectedIds.size() > Geometry::objectLimit || input.layers.size() > 64)
        return {};
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    setup(stream);
    auto ids = input.selectedIds;
    if (input.cutSelectedGraphics)
        ids.sort();
    else
        ids.clear();
    stream << qint32(1) << input.deviceProfile.outputFingerprint() << input.activePage
           << input.cutSelectedGraphics << ids << input.layers;
    qint64 pathElements = 0;
    for (const auto &art : input.items) {
        if (isCancelled(cancel))
            return {};
        pathElements += art.path.elementCount();
        if (pathElements > 2000000 || art.text.size() > 1000000 || art.settings.size() > 128 ||
            art.style.size() > 128 || (art.asset && art.asset->bytes.size() > 64 * 1024 * 1024) ||
            (art.reliefAsset && art.reliefAsset->png.size() > 64 * 1024 * 1024))
            return {};
        // Include helper marks and non-output objects: moving/hiding a reference
        // graphic must never silently retain an old output alignment.
        stream << art.id << art.kind << art.text << art.bounds << art.path << art.style
               << art.settings << art.extraOperations << art.engraveOutline << art.relief.toMap()
               << art.visible << art.page << art.toolLayer << art.layer << art.groupId;
        if (art.asset)
            stream << digest(art.asset->bytes);
        else
            stream << QByteArray{};
        if (art.reliefAsset)
            stream << digest(art.reliefAsset->png);
        else
            stream << QByteArray{};
        if (bytes.size() > 64 * 1024 * 1024 || stream.status() != QDataStream::Ok)
            return {};
    }
    return isCancelled(cancel) ? QByteArray{} : digest(bytes);
}
ManualRegistration::SourceSnapshot
ManualRegistration::SourceSnapshot::capture(const JobInput &input, const JobPlan &plan,
                                            const QByteArray &offlineFrame,
                                            const Cancellation &cancel) {
    SourceSnapshot source;
    source.m_error = validate(plan, cancel);
    if (!source.m_error.isEmpty())
        return source;
    source.m_profile = input.deviceProfile.outputFingerprint();
    if (!hash(offlineFrame) || source.m_profile != plan.deviceProfile().outputFingerprint()) {
        source.m_error =
            "Profil oder ausdrücklicher Offline-Koordinatenrahmen stimmt nicht überein.";
        return source;
    }
    source.m_frame = offlineFrame;
    source.m_geometry = geometryFingerprint(input, cancel);
    source.m_motion = plan.motionFingerprint(cancel);
    if (!hash(source.m_geometry) || source.m_motion.size() != 32) {
        source.m_error = "Quellstand überschreitet das Budget oder wurde abgebrochen.";
        return source;
    }
    source.m_fingerprint =
        digest(source.m_profile + source.m_frame + source.m_geometry + source.m_motion);
    return source;
}
bool ManualRegistration::SourceSnapshot::matches(const JobInput &input, const JobPlan &plan,
                                                 const QByteArray &offlineFrame,
                                                 const Cancellation &cancel) const {
    return valid() && !isCancelled(cancel) && m_frame == offlineFrame &&
           m_profile == input.deviceProfile.outputFingerprint() &&
           m_profile == plan.deviceProfile().outputFingerprint() &&
           m_geometry == geometryFingerprint(input, cancel) &&
           m_motion == plan.motionFingerprint(cancel) && !isCancelled(cancel);
}
QByteArray ManualRegistration::intentFingerprint(const Intent &intent) {
    if (!intent.source.valid())
        return {};
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    setup(stream);
    const auto &p = intent.parameters;
    stream << qint32(1) << intent.source.fingerprint() << p.designFirst << p.designSecond
           << p.targetFirst << p.targetSecond << qint32(p.scaling) << p.maximumResidualMm;
    return digest(bytes);
}
ManualRegistration::Result
ManualRegistration::apply(const JobInput &input, const JobPlan &canonical, const Intent &intent,
                          const QByteArray &offlineFrame,
                          const std::optional<JobPlacement::PositionSnapshot> &approach,
                          const Cancellation &cancel) {
    Result result{JobPlan(input.deviceProfile),
                  {{"enabled", true}, {"valid", false}, {"approachKnown", false}},
                  {}};
    auto fail = [&](QString reason) {
        result.plan = JobPlan(input.deviceProfile);
        result.plan.errors = {reason};
        result.plan.cancelled = isCancelled(cancel);
        result.summary["reason"] = reason;
        return result;
    };
    if (!intent.source.valid())
        return fail(intent.source.error());
    if (!intent.source.matches(input, canonical, offlineFrame, cancel))
        return fail("Registrierungsquelle, Auswahl, Profil oder Koordinatenrahmen ist veraltet. "
                    "Punkte erneut prüfen oder Registrierung ausdrücklich zurücksetzen.");
    const auto &p = intent.parameters;
    const auto bed = input.deviceProfile.workArea();
    for (const auto point : {p.designFirst, p.designSecond, p.targetFirst, p.targetSecond})
        if (!inside(point, bed))
            return fail("Alle Referenz- und Zielpunkte müssen endlich und innerhalb der "
                        "Profilfläche liegen.");
    if (p.scaling != Scaling::Rigid && p.scaling != Scaling::Uniform)
        return fail("Unbekanntes Skalierungsverfahren.");
    if (!std::isfinite(p.maximumResidualMm) || p.maximumResidualMm < 0 || p.maximumResidualMm > 10)
        return fail("Zulässiger Zweitpunktfehler muss zwischen 0 und 10 mm liegen.");
    const auto a = p.designSecond - p.designFirst, b = p.targetSecond - p.targetFirst;
    const double designLength = std::hypot(a.x(), a.y()), targetLength = std::hypot(b.x(), b.y());
    // Explicit local policy; LightBurn documents separation but no numeric threshold.
    if (designLength < 10 || targetLength < 10)
        return fail("Referenzpunkte benötigen mindestens 10 mm Abstand im Entwurf und im Ziel.");
    const double scale = p.scaling == Scaling::Uniform ? targetLength / designLength : 1;
    const double c = (a.x() * b.x() + a.y() * b.y()) / (designLength * targetLength);
    const double s = (a.x() * b.y() - a.y() * b.x()) / (designLength * targetLength);
    if (!std::isfinite(scale) || scale < .001 || scale > 1000 || !std::isfinite(c) ||
        !std::isfinite(s))
        return fail("Registrierungsmaßstab oder Drehung ist ungültig.");
    const double residual = p.scaling == Scaling::Rigid ? std::abs(targetLength - designLength) : 0;
    if (residual > p.maximumResidualMm + 1e-8)
        return fail("Zweitpunktfehler ohne Skalierung überschreitet die gewählte Grenze. Punkte "
                    "prüfen oder proportionale Skalierung ausdrücklich wählen.");
    const double dx = p.targetFirst.x() - scale * (c * p.designFirst.x() - s * p.designFirst.y());
    const double dy = p.targetFirst.y() - scale * (s * p.designFirst.x() + c * p.designFirst.y());
    result.transform = QTransform(scale * c, scale * s, -scale * s, scale * c, dx, dy);
    if (approach && (!approach->known || !approach->revision ||
                     approach->profileFingerprint != input.deviceProfile.outputFingerprint() ||
                     !hash(approach->coordinateFrameFingerprint) || !inside(approach->point, bed)))
        return fail("Übergebener Anfahrtspunkt ist unbekannt oder gehört nicht zum Profil.");
    result.plan = canonical;
    for (qsizetype i = 0; i < result.plan.moves.size(); ++i) {
        if (isCancelled(cancel))
            return fail("Registrierung abgebrochen.");
        auto move = result.plan.moves[i];
        move.from = result.transform.map(move.from);
        move.to = result.transform.map(move.to);
        if (i == 0 && move.rapid) {
            // The canonical first rapid's from is a planning convention, not a
            // measured 0/0. Keep it opaque when unknown; the encoder emits output-off
            // travel to the transformed target. Never manufacture a position proof.
            move.from = approach ? approach->point : canonical.moves.first().from;
        }
        result.plan.moves.replace(i, move);
    }
    if (approach && !result.plan.moves.first().rapid) {
        if (result.plan.moves.size() >= JobPlan::maximumMoves)
            return fail("Anfahrt überschreitet das Bewegungsbudget.");
        JobMoves moves;
        moves.reserve(result.plan.moves.size() + 1);
        const auto first = result.plan.moves.first();
        moves.append({approach->point, first.from, 3000, 0, first.layer, true, first.slice});
        for (const auto &move : result.plan.moves)
            moves.append(move);
        result.plan.moves = std::move(moves);
    }
    if (const auto error = validate(result.plan, cancel); !error.isEmpty())
        return fail(error);
    result.plan.workLength = result.plan.travelLength = result.plan.seconds =
        result.plan.minimumSeconds = 0;
    bool any = false;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    auto include = [&](QPointF point) {
        if (!any) {
            x0 = x1 = point.x();
            y0 = y1 = point.y();
            any = true;
        } else {
            x0 = std::min(x0, point.x());
            x1 = std::max(x1, point.x());
            y0 = std::min(y0, point.y());
            y1 = std::max(y1, point.y());
        }
    };
    for (const auto &move : result.plan.moves) {
        if (isCancelled(cancel))
            return fail("Registrierung abgebrochen.");
        const double length = QLineF(move.from, move.to).length();
        ((move.rapid || move.power <= 0) ? result.plan.travelLength : result.plan.workLength) +=
            length;
        result.plan.minimumSeconds += length / move.speed * 60;
        const double velocity = move.speed / 60;
        result.plan.seconds += length >= velocity * velocity / 240
                                   ? length / velocity + velocity / 240
                                   : 2 * std::sqrt(length / 240);
        if (!move.rapid) {
            include(move.from);
            include(move.to);
        }
    }
    result.summary["valid"] = true;
    result.summary["reason"] = "Manuelle Offline-Ausgaberegistrierung geprüft; keine physische "
                               "Messung oder Gerätefreigabe.";
    result.summary["sourceFingerprint"] = QString::fromLatin1(intent.source.fingerprint());
    result.summary["alignmentFingerprint"] = QString::fromLatin1(intentFingerprint(intent));
    result.summary["frameFingerprint"] = QString::fromLatin1(offlineFrame);
    result.summary["frameKind"] = "offline-g54-intent";
    result.summary["scaled"] = p.scaling == Scaling::Uniform;
    result.summary["scale"] = scale;
    result.summary["angleDegrees"] = std::atan2(s, c) * 180 / std::acos(-1.);
    result.summary["residualMm"] = residual;
    result.summary["designDistanceMm"] = designLength;
    result.summary["targetDistanceMm"] = targetLength;
    result.summary["motionBounds"] = rectangle(QRectF(QPointF(x0, y0), QPointF(x1, y1)));
    result.summary["outputMotionFingerprint"] =
        QString::fromLatin1(result.plan.motionFingerprint(cancel).toHex());
    result.summary["approachKnown"] = approach.has_value();
    return result;
}
