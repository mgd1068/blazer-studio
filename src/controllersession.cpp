#include "controllersession.h"
#include "controlleradapter.h"
#include "grbl.h"
#include <QSerialPortInfo>
namespace {
bool supported(const DeviceProfile &profile, QString &error) {
    if (profile.driverId() != "grbl-serial" || profile.transport() != "serial") {
        error = "Für diesen Controller/Transport ist kein Verbindungsadapter registriert.";
        return false;
    }
    return ControllerAdapters::projectProfile(profile, error).has_value();
}
} // namespace
std::unique_ptr<ControllerSession> ControllerSessions::create(const DeviceProfile &profile,
                                                              QString &error) {
    error.clear();
    if (!supported(profile, error))
        return {};
    return std::make_unique<GrblSession>(nullptr, profile);
}
QStringList ControllerSessions::availableEndpoints(const DeviceProfile &profile) {
    QString error;
    if (!supported(profile, error))
        return {};
    QStringList usb, other;
    for (const auto &port : QSerialPortInfo::availablePorts())
        (port.hasVendorIdentifier() ? usb : other).append(port.systemLocation());
    usb.sort();
    other.sort();
    return usb + other;
}
