#pragma once
#include <QByteArray>
#include <QVariantMap>

namespace SettingsBundle {
inline constexpr qsizetype maximumBytes = 16384;
struct Result {
    QVariantMap preferences;
    QString error;
    bool valid() const { return error.isEmpty() && preferences.size() == 3; }
};
Result validate(const QVariantMap &preferences);
Result read(const QByteArray &bytes);
QByteArray write(const QVariantMap &preferences);
} // namespace SettingsBundle
