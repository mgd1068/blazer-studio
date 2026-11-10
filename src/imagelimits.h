#pragma once
#include <QtGlobal>
namespace ImageLimits {
inline constexpr int side = 4096;
inline constexpr qint64 pixels = 16 * 1024 * 1024;
inline constexpr qint64 documentPixels = 32 * 1024 * 1024;
inline constexpr qint64 embeddedBytes = 40 * 1024 * 1024;
inline constexpr qint64 projectBytes = 64 * 1024 * 1024;
} // namespace ImageLimits
