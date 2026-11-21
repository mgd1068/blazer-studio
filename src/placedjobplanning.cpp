#include "placedjobplanning.h"
#include "geometry.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QJsonObject>
#include <cmath>
namespace {
bool pointValid(QPointF point) {
    return std::isfinite(point.x()) && std::isfinite(point.y()) && point.x() >= 0 &&
           point.y() >= 0 && point.x() <= Geometry::coordinateLimit &&
           point.y() <= Geometry::coordinateLimit;
}
bool hex(const QByteArray &bytes) {
    if (bytes.size() != 64)
        return false;
    for (const auto c : bytes)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}
bool outputs(const Artwork &art, const JobInput &input) {
    if (art.toolLayer || !art.visible || art.page != input.activePage)
        return false;
    QList<int> operations{art.layer};
    operations.append(art.extraOperations);
    for (const auto operation : operations) {
        if (operation < 0 || operation >= input.layers.size())
            return false;
        auto settings = input.layers[operation].toMap();
        if (operation == art.layer)
            for (auto it = art.settings.begin(); it != art.settings.end(); ++it)
                settings[it.key()] = it.value();
        if (settings.value("enabled").toBool() && settings.value("power").toDouble() > 0)
            return true;
    }
    return false;
}
struct GeometrySnapshot {
    QRectF bounds;
    QByteArray fingerprint;
    bool any = false;
};
GeometrySnapshot graphics(const JobInput &input, const Cancellation &cancel) {
    GeometrySnapshot result;
    if (input.items.size() > Geometry::objectLimit)
        return result;
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::BigEndian);
    stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
    stream << qint32(1) << input.deviceProfile.outputFingerprint() << qint32(input.activePage)
           << input.layers;
    for (const auto &art : input.items) {
        if (isCancelled(cancel))
            return {};
        if (!outputs(art, input))
            continue;
        result.bounds = result.any ? result.bounds.united(art.bounds) : art.bounds;
        result.any = true;
        stream << art.id << art.kind << qint32(art.layer) << qint32(art.page) << art.text
               << art.bounds << art.path << art.style << art.settings << art.extraOperations
               << art.engraveOutline << art.relief.toMap();
        if (art.asset)
            stream << QCryptographicHash::hash(art.asset->bytes, QCryptographicHash::Sha256);
        if (art.reliefAsset)
            stream << QCryptographicHash::hash(art.reliefAsset->png, QCryptographicHash::Sha256);
    }
    if (result.any && !isCancelled(cancel))
        result.fingerprint = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    return result;
}
QVariantMap bounds(const QRectF &rectangle) {
    return {{"x", rectangle.x()},
            {"y", rectangle.y()},
            {"width", rectangle.width()},
            {"height", rectangle.height()}};
}
} // namespace
bool PlacedJobPlanning::validPosition(const JobPlacement::PositionSnapshot &position,
                                      const DeviceProfile &profile) {
    return position.known && position.revision > 0 && pointValid(position.point) &&
           position.point.x() <= profile.workArea().width() &&
           position.point.y() <= profile.workArea().height() &&
           position.profileFingerprint == profile.outputFingerprint() &&
           hex(position.coordinateFrameFingerprint);
}
bool PlacedJobPlanning::defaultIntent(const Intent &intent) {
    return intent == Intent{};
}
QJsonObject PlacedJobPlanning::serializeIntent(const Intent &intent) {
    return {{"version", 1},
            {"mode", int(intent.mode)},
            {"anchor", int(intent.anchor)},
            {"userOrigin", intent.userOrigin
                               ? QJsonValue(QJsonObject{{"x", intent.userOrigin->x()},
                                                        {"y", intent.userOrigin->y()}})
                               : QJsonValue(QJsonValue::Null)}};
}
std::optional<PlacedJobPlanning::Intent> PlacedJobPlanning::parseIntent(const QJsonValue &json) {
    if (!json.isObject())
        return {};
    const auto object = json.toObject();
    if (object.size() != 4 || !object.contains("version") || !object.contains("mode") ||
        !object.contains("anchor") || !object.contains("userOrigin") ||
        !object["version"].isDouble() || object["version"].toDouble() != 1 ||
        !object["mode"].isDouble() || !object["anchor"].isDouble())
        return {};
    const double mode = object["mode"].toDouble(), anchor = object["anchor"].toDouble();
    if (!std::isfinite(mode) || mode < 0 || mode > 2 || std::floor(mode) != mode ||
        !std::isfinite(anchor) || anchor < 0 || anchor > 8 || std::floor(anchor) != anchor)
        return {};
    Intent intent;
    intent.mode = JobPlacement::Mode(int(mode));
    intent.anchor = JobPlacement::Anchor(int(anchor));
    if (!object["userOrigin"].isNull()) {
        const auto origin = object["userOrigin"].toObject();
        if (!object["userOrigin"].isObject() || origin.size() != 2 || !origin.contains("x") ||
            !origin.contains("y") || !origin["x"].isDouble() || !origin["y"].isDouble())
            return {};
        const QPointF point(origin["x"].toDouble(), origin["y"].toDouble());
        if (!pointValid(point))
            return {};
        intent.userOrigin = point;
    }
    return intent;
}
PlacedJobPlanning::Result PlacedJobPlanning::finish(const Input &input, JobPlan canonical,
                                                    const Cancellation &cancel) {
    const auto &context = input.placement;
    Result result{std::move(canonical),
                  {{"mode", int(context.intent.mode)},
                   {"anchor", int(context.intent.anchor)},
                   {"originScope", "design"},
                   {"approachKnown", false}}};
    auto fail = [&](const QString &reason) {
        result.plan = JobPlan(input.document.deviceProfile);
        result.plan.errors = {reason};
        result.summary["valid"] = false;
        result.summary["reason"] = reason;
        return result;
    };
    if (isCancelled(cancel)) {
        auto aborted = fail("Platzierungsberechnung abgebrochen.");
        aborted.plan.cancelled = true;
        return aborted;
    }
    if (result.plan.deviceProfile().outputFingerprint() !=
        input.document.deviceProfile.outputFingerprint())
        return fail("Berechnungsprofil passt nicht zum unveränderlichen Quellstand.");
    if (!result.plan.valid()) {
        result.summary["valid"] = false;
        result.summary["reason"] =
            result.plan.cancelled ? "Berechnung abgebrochen." : result.plan.errors.join("; ");
        return result;
    }
    const auto geometry = graphics(input.document, cancel);
    if (geometry.any)
        result.summary["sourceGraphicsBounds"] = bounds(geometry.bounds);
    JobPlacement::SourceSnapshot source;
    if (geometry.any && !geometry.fingerprint.isEmpty())
        source = JobPlacement::SourceSnapshot::capture(result.plan, geometry.bounds,
                                                       geometry.fingerprint,
                                                       JobPlacement::OriginScope::Design, cancel);
    if (source.valid())
        result.summary["sourceFingerprint"] = QString::fromLatin1(source.fingerprint());
    if (isCancelled(cancel)) {
        auto aborted = fail("Platzierungsberechnung abgebrochen.");
        aborted.plan.cancelled = true;
        return aborted;
    }
    // Preserve legacy absolute plans exactly, including their existing tolerance.
    if (context.intent.mode == JobPlacement::Mode::Absolute && !context.startPosition) {
        result.summary["valid"] = true;
        result.summary["reason"] =
            "Absolute Entwurfskoordinaten; tatsächlicher Anfahrtspunkt nicht übergeben.";
        if (geometry.any)
            result.summary["graphicsBounds"] = bounds(geometry.bounds);
        result.summary["offsetX"] = 0.;
        result.summary["offsetY"] = 0.;
        return result;
    }
    if (!geometry.any || geometry.fingerprint.isEmpty())
        return fail("Keine wirksame Ausgabegeometrie für den Ursprung vorhanden.");
    if (!source.valid())
        return fail(source.error());
    JobPlacement::Request request;
    request.mode = context.intent.mode;
    request.anchor = context.intent.anchor;
    request.expectedSourceFingerprint = source.fingerprint();
    request.startPosition = context.startPosition;
    if (request.mode == JobPlacement::Mode::UserOrigin) {
        if (!context.intent.userOrigin || !context.confirmedOrigin ||
            context.confirmedOrigin->point.x() != context.intent.userOrigin->x() ||
            context.confirmedOrigin->point.y() != context.intent.userOrigin->y())
            return fail("Benutzerursprung für den aktuellen Positionsrahmen ausdrücklich erfassen "
                        "oder bestätigen.");
        request.userOrigin = context.confirmedOrigin;
    }
    const auto placed = JobPlacement::place(result.plan, source, request, cancel);
    if (!placed.valid())
        return fail(placed.error());
    result.plan = placed.plan();
    result.summary["valid"] = true;
    result.summary["reason"] = "Offline-Platzierung geprüft; keine physische Gerätefreigabe.";
    result.summary["graphicsBounds"] = bounds(placed.placedGraphicsBounds());
    result.summary["offsetX"] = placed.translation().x();
    result.summary["offsetY"] = placed.translation().y();
    result.summary["sourceFingerprint"] = QString::fromLatin1(placed.sourceFingerprint());
    result.summary["requestFingerprint"] = QString::fromLatin1(placed.requestFingerprint());
    result.summary["approachKnown"] = placed.explicitApproach();
    return result;
}
PlacedJobPlanning::Result PlacedJobPlanning::build(const Input &input, const Cancellation &cancel) {
    return finish(input, JobPlanner::build(input.document, cancel), cancel);
}
