#pragma once
#include <QByteArray>
#include <QList>
#include <QPainterPath>
#include <QString>

namespace PlotImport {
struct Result {
    QList<QPainterPath> paths;
    QString error;
};
// Standalone, unscaled HP-GL geometry. No device access or machine settings.
// Returned paths use millimetres, world Y-up, with a shared lower-left at (0,0).
Result hpgl(const QByteArray &bytes);
} // namespace PlotImport
