#include "controlleradapter.h"
#include "job.h"
#include <QJsonDocument>
#include <cmath>
namespace {
QString num(double n) {
    if (std::abs(n) < .0005)
        n = 0;
    return QString::number(n, 'f', 3);
}
bool number(const QJsonValue &v, double minimum, double maximum) {
    return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= minimum &&
           v.toDouble() <= maximum;
}
bool keys(const QJsonObject &object, const QStringList &allowed) {
    for (auto i = object.begin(); i != object.end(); ++i)
        if (!allowed.contains(i.key()))
            return false;
    return true;
}
class GrblControllerAdapter final : public ControllerAdapter {
  public:
    QString outputError(const DeviceProfile &profile) const override {
        if (profile.driverId() != "grbl-serial" || profile.transport() != "serial")
            return "Für dieses Profil ist kein GRBL-Ausgabeadapter vorhanden.";
        const auto scale = profile.driverSettings()["powerScale"];
        if (!number(scale, 1, 1000000000) || std::floor(scale.toDouble()) != scale.toDouble())
            return "GRBL-Ausgabe benötigt eine ganzzahlige Leistungsskala von 1 bis 1000000000.";
        if (!profile.declares("motion-xy") || !profile.declares("dynamic-power"))
            return "Der vorhandene GRBL-Ausgabeadapter benötigt XY und dynamische Leistung.";
        if (profile.driverSettings()["originCode"] != 0 ||
            profile.driverSettings()["zEnabled"] != false)
            return "Der vorhandene GRBL-Ausgabeadapter unterstützt diesen Achsen-/Ursprungsvertrag "
                   "nicht.";
        return {};
    }
    std::optional<DeviceProfile> projectProfile(const DeviceProfile &profile,
                                                QString &error) const override {
        auto fail = [&](QString message) -> std::optional<DeviceProfile> {
            error = std::move(message);
            return {};
        };
        if (profile.driverId() != "grbl-serial" || profile.transport() != "serial")
            return fail("Für diesen Controller ist noch kein Projektprofilschema eingebunden.");
        const auto settings = profile.driverSettings();
        if (!keys(settings, {"baudRate", "powerScale", "focusPower", "zEnabled",
                             "homeOnStartupConfigured", "originCode"}) ||
            !number(settings["baudRate"], 1200, 4000000) ||
            std::floor(settings["baudRate"].toDouble()) != settings["baudRate"].toDouble() ||
            !number(settings["focusPower"], 0, 100) || settings["zEnabled"] != false ||
            !settings["homeOnStartupConfigured"].isBool() || settings["originCode"] != 0 ||
            !outputError(profile).isEmpty())
            return fail(
                "Projektprofil benötigt bekannte GRBL-XY-Ausgabeparameter und Ursprung links "
                "unten; keine Anschluss- oder Zugangsdaten.");
        if (profile.workArea().width() > 2000 || profile.workArea().height() > 2000)
            return fail("Der aktuelle Editor unterstützt Arbeitsflächen bis 2000 × 2000 mm.");
        auto data = profile.toJson();
        data["provenance"] = QJsonObject{};
        return DeviceProfile::parse(QJsonDocument(data).toJson(), error);
    }
    ControllerProgram encode(const JobPlan &plan, const QByteArray &sourceFingerprint,
                             const Cancellation &cancel) const override {
        const auto &profile = plan.deviceProfile();
        const auto error = outputError(profile);
        if (!error.isEmpty())
            return ControllerProgram::failure(profile, sourceFingerprint, error);
        if (!plan.valid())
            return ControllerProgram::failure(profile, sourceFingerprint,
                                              "Kein gültiger Bewegungsplan.");
        const auto bed = profile.workArea();
        for (const auto &move : plan.moves) {
            if (isCancelled(cancel))
                return ControllerProgram::failure(profile, sourceFingerprint,
                                                  "Ausgabe abgebrochen.", true);
            if (!std::isfinite(move.speed) || move.speed < 1 || move.speed > 100000 ||
                !std::isfinite(move.power) || move.power < 0 || move.power > 100)
                return ControllerProgram::failure(
                    profile, sourceFingerprint,
                    "Ungültiger Vorschub oder Prozentwert im Ausgabeplan.");
            for (auto point : {move.from, move.to})
                if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || point.x() < -.00001 ||
                    point.y() < -.00001 || point.x() > bed.width() + .00001 ||
                    point.y() > bed.height() + .00001)
                    return ControllerProgram::failure(
                        profile, sourceFingerprint,
                        "Ausgabeplan außerhalb des Profil-Arbeitsraums.");
        }
        const double powerScale = profile.driverSettings()["powerScale"].toDouble();
        QStringList lines{"M5",
                          "(Blazer Studio: GRBL laser mode, $30=" +
                              QString::number(powerScale, 'f', 0) + ", $32=1, G54)",
                          "G21",
                          "G90",
                          "G54",
                          "G17",
                          "G94",
                          "S0"};
        bool emitting = false;
        double lastPower = -1, lastFeed = -1;
        int lastSlice = 0;
        for (const auto &m : plan.moves) {
            if (isCancelled(cancel))
                return ControllerProgram::failure(profile, sourceFingerprint,
                                                  "Ausgabe abgebrochen.", true);
            if (m.slice != 0 && m.slice != lastSlice) {
                lines << (m.slice > 0 ? "(Relief slice " : "(Relief cleanup after ") +
                             QString::number(m.slice < 0 ? -qint64(m.slice) : qint64(m.slice)) +
                             ")";
                lastSlice = m.slice;
            }
            if (m.rapid) {
                if (emitting) {
                    lines << "M5";
                    emitting = false;
                }
                lines << "G0 X" + num(m.to.x()) + " Y" + num(m.to.y());
            } else {
                if (!emitting) {
                    lines << "M4 S" + QString::number(qRound64(m.power * powerScale / 100.));
                    emitting = true;
                    lastPower = m.power;
                }
                auto line = "G1 X" + num(m.to.x()) + " Y" + num(m.to.y());
                // A standalone S change drains GRBL's planner. Keep power changes on
                // their motion block so raster overscan remains a continuous move.
                if (m.power != lastPower) {
                    line += " S" + QString::number(qRound64(m.power * powerScale / 100.));
                    lastPower = m.power;
                }
                if (m.speed != lastFeed) {
                    line += " F" + num(m.speed);
                    lastFeed = m.speed;
                }
                lines << line;
            }
        }
        lines << "M5 S0" << "G0";
        if (isCancelled(cancel))
            return ControllerProgram::failure(profile, sourceFingerprint, "Ausgabe abgebrochen.",
                                              true);
        return ControllerProgram::success(profile, sourceFingerprint, "text/x-gcode",
                                          (lines.join('\n') + '\n').toUtf8());
    }
};
} // namespace
const ControllerAdapter &grblControllerAdapter() {
    static const GrblControllerAdapter adapter;
    return adapter;
}
