#include "controlleradapter.h"

const ControllerAdapter &grblControllerAdapter();

ControllerProgram::ControllerProgram(const DeviceProfile &profile, QByteArray source)
    : m_driverId(profile.driverId()), m_transport(profile.transport()),
      m_profileFingerprint(profile.outputFingerprint()), m_sourceFingerprint(std::move(source)) {
}
ControllerProgram ControllerProgram::success(const DeviceProfile &profile, QByteArray source,
                                             QString type, QByteArray payload) {
    ControllerProgram program(profile, std::move(source));
    if (type.isEmpty() || payload.isEmpty() || payload.size() > 64 * 1024 * 1024 ||
        program.m_sourceFingerprint.isEmpty()) {
        program.m_errors = {"Ungültige oder zu große Controllerausgabe."};
        return program;
    }
    program.m_mediaType = std::move(type);
    program.m_payload = std::move(payload);
    return program;
}
ControllerProgram ControllerProgram::failure(const DeviceProfile &profile, QByteArray source,
                                             QString error, bool cancelled) {
    ControllerProgram program(profile, std::move(source));
    program.m_cancelled = cancelled;
    program.m_errors = {error.isEmpty() ? QString("Keine Controllerausgabe verfügbar.")
                                        : std::move(error)};
    return program;
}
bool ControllerProgram::matches(const DeviceProfile &profile, const QByteArray &source) const {
    return !source.isEmpty() && m_driverId == profile.driverId() &&
           m_transport == profile.transport() &&
           m_profileFingerprint == profile.outputFingerprint() && m_sourceFingerprint == source;
}
QStringList ControllerProgram::gcodeLines() const {
    if (!valid() || m_mediaType != "text/x-gcode")
        return {};
    auto lines = QString::fromUtf8(m_payload).split('\n');
    if (lines.last().isEmpty())
        lines.removeLast();
    return lines;
}
const ControllerAdapter *ControllerAdapters::find(const DeviceProfile &profile) {
    if (profile.driverId() == "grbl-serial" && profile.transport() == "serial")
        return &grblControllerAdapter();
    return nullptr;
}
QString ControllerAdapters::outputError(const DeviceProfile &profile) {
    const auto adapter = find(profile);
    return adapter
               ? adapter->outputError(profile)
               : QString("Für diesen Controller/Transport ist kein Ausgabeadapter registriert.");
}
std::optional<DeviceProfile> ControllerAdapters::projectProfile(const DeviceProfile &profile,
                                                                QString &error) {
    const auto adapter = find(profile);
    if (!adapter) {
        error = "Für diesen Controller/Transport ist kein Projektprofilschema registriert.";
        return {};
    }
    return adapter->projectProfile(profile, error);
}
