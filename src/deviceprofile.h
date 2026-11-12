#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QSizeF>
#include <QString>
#include <QVariantMap>
#include <optional>
#include <utility>

// Immutable configuration data. Parsing a descriptor never opens a connection
// or grants a declared capability to a controller.
class DeviceProfile {
  public:
    static std::optional<DeviceProfile> parse(const QByteArray &bytes, QString &error);
    static std::optional<DeviceProfile> fromLegacyM3(const QJsonObject &source, QString &error);
    static DeviceProfile m3Reference();
    QString id() const { return data["id"].toString(); }
    QString name() const { return data["name"].toString(); }
    QString driverId() const { return data["driver"].toObject()["id"].toString(); }
    QString transport() const { return data["driver"].toObject()["transport"].toString(); }
    QSizeF workArea() const;
    QJsonObject driverSettings() const { return data["driver"].toObject()["settings"].toObject(); }
    QJsonObject declaredCapabilities() const { return data["capabilities"].toObject(); }
    bool declares(const QString &capability) const {
        return declaredCapabilities()[capability].toBool();
    }
    QJsonObject toJson() const { return data; }
    QByteArray outputFingerprint() const;
    QVariantMap legacyUiProperties() const;

  private:
    explicit DeviceProfile(QJsonObject value) : data(std::move(value)) {}
    static std::optional<DeviceProfile> fromObject(QJsonObject value, QString &error);
    QJsonObject data;
};

namespace DeviceBackends {
// Temporary migration boundary: only the existing M3 configuration is wired
// through every runtime consumer. Other valid descriptors remain non-executable
// until planner/editor/driver binding is implemented and tested for them.
QString runtimeSupportError(const DeviceProfile &profile);
} // namespace DeviceBackends
