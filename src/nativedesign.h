#pragma once
#include "relief.h"
#include <QByteArray>
#include <QList>
#include <QPainterPath>
#include <QRectF>
#include <QString>
namespace NativeDesign {
struct Element {
    QPainterPath path;
    std::shared_ptr<const ReliefAsset> image;
    QRectF bounds;
    QString group;
    bool visible = true;
};
struct Result {
    QList<Element> elements;
    QString error;
};
// Geometry only. No CNC/laser operations, device settings, or external files.
Result read(const QByteArray &bytes, bool millmage);
} // namespace NativeDesign
