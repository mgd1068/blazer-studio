#include "vectorinspection.h"
#include <QCryptographicHash>
#include <QLineF>
#include <QSet>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>
namespace VectorInspection {
namespace {
constexpr int maximumObjects = 1000, maximumElements = 4000, maximumSegments = 20000,
              maximumPairs = 500000, maximumFindings = 500;
constexpr double flattenTolerance = .02, epsilon = .000001;
bool finite(QPointF p) {
    return std::isfinite(p.x()) && std::isfinite(p.y());
}
bool near(QPointF a, QPointF b) {
    return QLineF(a, b).length() <= epsilon;
}
double cross(QPointF a, QPointF b, QPointF c) {
    return (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
}
struct Work {
    Report report;
    Cancellation cancel;
    bool running() {
        if (isCancelled(cancel)) {
            report.cancelled = true;
            report.complete = false;
            return false;
        }
        return report.complete;
    }
    void finding(QString code, QString label, QStringList ids = {}, int contour = -1,
                 QPointF position = {}, QString severity = "warning") {
        if (report.findings.size() >= maximumFindings) {
            limit();
            return;
        }
        report.findings.append({std::move(code), std::move(severity), std::move(label),
                                std::move(ids), contour, position});
    }
    void limit() {
        if (!report.complete)
            return;
        report.complete = false;
        report.findings.append(
            {"work-limit", "info", "Prüfung unvollständig: Arbeitsgrenze erreicht.", {}, -1, {}});
    }
};
struct Contour {
    QVector<QPointF> points;
};
bool append(Contour &contour, QPointF point, Work &work) {
    if (!finite(point) || std::abs(point.x()) > 1000000 || std::abs(point.y()) > 1000000) {
        work.limit();
        return false;
    }
    if (!contour.points.isEmpty() && ++work.report.segmentCount > maximumSegments) {
        work.limit();
        return false;
    }
    contour.points.append(point);
    return work.running();
}
bool cubic(Contour &contour, QPointF a, QPointF b, QPointF c, QPointF d, int depth, Work &work) {
    if (!work.running())
        return false;
    const double length = QLineF(a, d).length();
    const double flat = length > epsilon
                            ? std::max(std::abs(cross(a, d, b)), std::abs(cross(a, d, c))) / length
                            : std::max(QLineF(a, b).length(), QLineF(a, c).length());
    if (flat <= flattenTolerance &&
        QLineF(a, b).length() + QLineF(b, c).length() + QLineF(c, d).length() - length <=
            flattenTolerance)
        return append(contour, d, work);
    if (depth >= 12) {
        work.limit();
        return false;
    }
    const auto ab = (a + b) / 2, bc = (b + c) / 2, cd = (c + d) / 2, abc = (ab + bc) / 2,
               bcd = (bc + cd) / 2, mid = (abc + bcd) / 2;
    return cubic(contour, a, ab, abc, mid, depth + 1, work) &&
           cubic(contour, mid, bcd, cd, d, depth + 1, work);
}
QList<Contour> flatten(const QPainterPath &path, Work &work) {
    QList<Contour> contours;
    if (path.elementCount() > maximumElements) {
        work.limit();
        return {};
    }
    for (int i = 0; i < path.elementCount() && work.running(); ++i) {
        const auto element = path.elementAt(i);
        const QPointF point(element.x, element.y);
        if (element.isMoveTo()) {
            contours.append(Contour{});
            if (!append(contours.last(), point, work))
                break;
        } else if (element.isLineTo() && !contours.isEmpty()) {
            if (!append(contours.last(), point, work))
                break;
        } else if (element.type == QPainterPath::CurveToElement && !contours.isEmpty() &&
                   i + 2 < path.elementCount()) {
            const auto control = path.elementAt(i + 1), end = path.elementAt(i + 2);
            if (!finite(point) || !finite({control.x, control.y}) || !finite({end.x, end.y})) {
                work.limit();
                break;
            }
            if (!cubic(contours.last(), contours.last().points.last(), point,
                       {control.x, control.y}, {end.x, end.y}, 0, work))
                break;
            i += 2;
        } else {
            work.limit();
            break;
        }
    }
    return contours;
}
QString pointKey(QPointF p) {
    return QString::number(qRound64(p.x() * 1000)) + "," + QString::number(qRound64(p.y() * 1000));
}
QByteArray contourKey(const QVector<QPointF> &points) {
    QStringList edges;
    for (int i = 1; i < points.size(); ++i) {
        if (near(points[i - 1], points[i]))
            continue;
        auto a = pointKey(points[i - 1]), b = pointKey(points[i]);
        if (b < a)
            std::swap(a, b);
        edges.append(a + ":" + b);
    }
    std::sort(edges.begin(), edges.end());
    return QCryptographicHash::hash(edges.join(';').toUtf8(), QCryptographicHash::Sha256);
}
bool intersects(QPointF a, QPointF b, QPointF c, QPointF d, QPointF &at) {
    const double abC = cross(a, b, c), abD = cross(a, b, d), cdA = cross(c, d, a),
                 cdB = cross(c, d, b);
    if (((abC > epsilon && abD < -epsilon) || (abC < -epsilon && abD > epsilon)) &&
        ((cdA > epsilon && cdB < -epsilon) || (cdA < -epsilon && cdB > epsilon))) {
        QLineF(a, b).intersects(QLineF(c, d), &at);
        return true;
    }
    // Positive-length collinear overlap is repeated output; mere corner contact is not.
    if (std::abs(abC) <= epsilon && std::abs(abD) <= epsilon && !near(a, b) && !near(c, d)) {
        const bool x = std::abs(b.x() - a.x()) >= std::abs(b.y() - a.y());
        const auto value = [x](QPointF p) { return x ? p.x() : p.y(); };
        const double lo = std::max(std::min(value(a), value(b)), std::min(value(c), value(d)));
        const double hi = std::min(std::max(value(a), value(b)), std::max(value(c), value(d)));
        if (hi - lo > epsilon) {
            at = (a + b + c + d) / 4;
            return true;
        }
    }
    return false;
}
bool svgBudget(const Artwork &art) {
    if (art.kind != "svg" || !art.asset)
        return true;
    if (art.asset->bytes.size() > 65536)
        return false;
    QXmlStreamReader xml(art.asset->bytes);
    int nodes = 0;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && ++nodes > 1000)
            return false;
    }
    return !xml.hasError();
}
} // namespace
QVariantMap Report::toVariantMap() const {
    QVariantList rows;
    for (const auto &finding : findings)
        rows.append(QVariantMap{{"code", finding.code},
                                {"severity", finding.severity},
                                {"label", finding.label},
                                {"objectIds", finding.objectIds},
                                {"contourIndex", finding.contourIndex},
                                {"xMm", finding.positionMm.x()},
                                {"yMm", finding.positionMm.y()}});
    return {{"findings", rows},
            {"complete", complete},
            {"cancelled", cancelled},
            {"inspectedObjects", inspectedObjects},
            {"segmentCount", segmentCount},
            {"pairChecks", pairChecks},
            {"advisoryOnly", true}};
}
Report inspect(const JobInput &snapshot, const QStringList &selectedIds,
               const Cancellation &cancel) {
    Work work{{}, cancel};
    QHash<QByteArray, QPair<QString, int>> cuts;
    const QSet<QString> selected(selectedIds.begin(), selectedIds.end());
    const auto bed = snapshot.deviceProfile.workArea();
    for (const auto &art : snapshot.items) {
        if (!work.running())
            break;
        if (!art.visible || art.toolLayer || art.page != snapshot.activePage ||
            (!selected.isEmpty() && !selected.contains(art.id)))
            continue;
        if (art.kind == "image" || art.kind == "relief")
            continue;

        auto operations = art.extraOperations;
        operations.prepend(art.layer);
        bool output = false, cut = false;
        for (int op : operations) {
            if (op < 0 || op >= snapshot.layers.size()) {
                work.finding("geometry-unavailable", "Ungültige Bearbeitungszuordnung.", {art.id});
                continue;
            }
            auto settings = snapshot.layers[op].toMap();
            if (op == art.layer)
                for (auto it = art.settings.begin(); it != art.settings.end(); ++it)
                    settings[it.key()] = it.value();
            if (!settings["enabled"].toBool() || settings["power"].toDouble() <= 0)
                continue;
            output = true;
            if (settings["mode"] == "Schnitt")
                cut = true;
        }
        if (!output)
            continue;
        if (work.report.inspectedObjects >= maximumObjects ||
            art.path.elementCount() > maximumElements || !svgBudget(art) ||
            art.text.size() > 1000) {
            work.limit();
            break;
        }
        ++work.report.inspectedObjects;
        QString error;
        const auto path = JobPlanner::geometry(art, error, false);
        if (!error.isEmpty()) {
            work.finding("geometry-unavailable",
                         "Vektorgeometrie nicht vollständig prüfbar: " + error, {art.id});
            work.report.complete = false;
            break;
        }
        const auto bounds = path.boundingRect();
        if (!std::isfinite(bounds.x()) || !std::isfinite(bounds.y()) ||
            !std::isfinite(bounds.width()) || !std::isfinite(bounds.height()) ||
            bounds.left() < -epsilon || bounds.top() < -epsilon ||
            bounds.right() > bed.width() + epsilon || bounds.bottom() > bed.height() + epsilon)
            work.finding("outside-work-area",
                         "Vektorgeometrie liegt außerhalb der Profil-Arbeitsfläche.", {art.id}, -1,
                         finite(bounds.center()) ? bounds.center() : QPointF());
        const auto contours = flatten(path, work);
        if (contours.isEmpty() && work.running())
            work.finding("degenerate-contour", "Objekt enthält keine nutzbare Vektorkontur.",
                         {art.id});
        for (int contourIndex = 0; contourIndex < contours.size() && work.running();
             ++contourIndex) {
            const auto &points = contours[contourIndex].points;
            if (points.isEmpty())
                continue;
            double length = 0;
            bool zero = false;
            for (int i = 1; i < points.size(); ++i) {
                const double edge = QLineF(points[i - 1], points[i]).length();
                length += edge;
                zero = zero || edge <= epsilon;
            }
            if (length <= epsilon || zero)
                work.finding("degenerate-contour",
                             length <= epsilon ? "Kontur ohne nutzbare Länge."
                                               : "Kontur enthält Null-Längen-Segmente.",
                             {art.id}, contourIndex, points.first());
            if (length <= epsilon)
                continue;
            const bool closed = near(points.first(), points.last());
            if (cut && !closed)
                work.finding(
                    "open-cut-contour",
                    "Offene Schnittkontur: beabsichtigte Linie oder fehlenden Abschluss prüfen.",
                    {art.id}, contourIndex, points.first());
            if (cut) {
                const auto key = contourKey(points);
                if (cuts.contains(key)) {
                    const auto prior = cuts[key];
                    work.finding("duplicate-cut-contour",
                                 "Deckungsgleiche Schnittkonturen: doppelte Bearbeitung prüfen.",
                                 {prior.first, art.id}, contourIndex, points.first());
                } else
                    cuts.insert(key, {art.id, contourIndex});
            }
            bool found = false;
            // Adjacent edges share a normal corner, but reversing across the
            // same edge is repeated output and must not be skipped as adjacency.
            QSet<QString> seenEdges;
            for (int i = 1; i < points.size(); ++i) {
                if (near(points[i - 1], points[i]))
                    continue;
                auto a = pointKey(points[i - 1]), b = pointKey(points[i]);
                if (b < a)
                    std::swap(a, b);
                const auto edge = a + ":" + b;
                if (seenEdges.contains(edge)) {
                    work.finding("self-intersection", "Kontur überfährt dasselbe Segment mehrfach.",
                                 {art.id}, contourIndex, points[i]);
                    found = true;
                    break;
                }
                seenEdges.insert(edge);
            }
            for (int a = 1; a < points.size() && !found && work.running(); ++a)
                for (int b = a + 2; b < points.size() && work.running(); ++b) {
                    if (closed && a == 1 && b == points.size() - 1)
                        continue;
                    if (work.report.pairChecks >= maximumPairs) {
                        work.limit();
                        break;
                    }
                    ++work.report.pairChecks;
                    QPointF at;
                    if (intersects(points[a - 1], points[a], points[b - 1], points[b], at)) {
                        work.finding("self-intersection",
                                     "Kontur kreuzt oder überlagert sich selbst.", {art.id},
                                     contourIndex, at);
                        found = true;
                        break;
                    }
                }
        }
        // Subpaths are independent: never invent a connecting segment. Only
        // actual edge crossings/overlap between contours are advisory findings.
        for (int first = 0; first < contours.size() && work.running(); ++first)
            for (int second = first + 1; second < contours.size() && work.running(); ++second) {
                const auto &a = contours[first].points, &b = contours[second].points;
                if (contourKey(a) == contourKey(b))
                    continue; // Already reported as duplicate cut geometry.
                bool found = false;
                for (int i = 1; i < a.size() && !found && work.running(); ++i)
                    for (int j = 1; j < b.size() && work.running(); ++j) {
                        if (work.report.pairChecks >= maximumPairs) {
                            work.limit();
                            break;
                        }
                        ++work.report.pairChecks;
                        QPointF at;
                        if (intersects(a[i - 1], a[i], b[j - 1], b[j], at)) {
                            work.finding(
                                "self-intersection",
                                "Teilkonturen desselben Objekts kreuzen oder überlagern sich.",
                                {art.id}, second, at);
                            found = true;
                            break;
                        }
                    }
            }
    }
    return work.report;
}
} // namespace VectorInspection
