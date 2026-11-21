#pragma once
#include <QString>
#include <cmath>
#include <limits>

namespace GrblWireNumbers {
// Keep encoding and signed-envelope preflight on exactly the same decimal
// conversion. std::round(value*1000)/1000 is not equivalent at binary/decimal
// ties to QString::number(value, 'f', 3), e.g. 0.0045 on the supported Qt build.
inline QString format(double value) {
    if (std::abs(value) < .0005) value = 0;
    return QString::number(value, 'f', 3);
}
inline double coordinate(double value) {
    if (!std::isfinite(value)) return std::numeric_limits<double>::quiet_NaN();
    bool ok = false;
    const auto encoded = format(value).toDouble(&ok);
    return ok ? encoded : std::numeric_limits<double>::quiet_NaN();
}
} // namespace GrblWireNumbers
