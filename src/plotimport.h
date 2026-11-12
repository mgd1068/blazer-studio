#pragma once
#include <QByteArray>
#include <QList>
#include <QPainterPath>
#include <QString>

namespace PlotImport {
struct Result {
    QList<QPainterPath> paths;
    QString error;
    QString interpretation{};
};
// Standalone, unscaled HP-GL geometry. No device access or machine settings.
// Returned paths use millimetres, world Y-up, with a shared lower-left at (0,0).
Result hpgl(const QByteArray &bytes);
// Standalone nominal XY paths, never executable commands. M/S words select
// spindle-aware filtering; otherwise non-rapid feed paths are imported.
Result gcode(const QByteArray &bytes);
} // namespace PlotImport
