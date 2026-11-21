#pragma once
#include <QPainterPath>
#include <QString>

namespace Barcode {
inline constexpr int maximumCharacters = 128;
inline constexpr int quietModules = 10;
// A deliberately bounded Code 128 subset: Start B, printable ASCII, checksum, Stop.
// Geometry is normalized to [0,1], including ten empty modules on each side.
struct Result {
    QPainterPath path;
    int modules = 0;
    QString error;
    bool valid() const { return error.isEmpty() && modules > 0; }
};
Result code128B(const QString &text);
}
