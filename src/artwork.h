#pragma once
#include "relief.h"
#include "svgasset.h"
#include <QPainterPath>
#include <QVariantMap>

struct Artwork {
    QString id, name, kind, text;
    QRectF bounds; // mm, lower-left origin, y points up
    int layer = 0;
    bool visible = true;
    std::shared_ptr<const SvgAsset> asset;
    QString groupId{}, groupName{};
    QVariantMap settings{};
    std::shared_ptr<const ReliefAsset> reliefAsset{};
    ReliefSettings relief{};
    QPainterPath path{}; // normalized coordinates, lower-left convention
    QVariantMap style{};
    QList<int> extraOperations{};
    int page = 0;
    int toolLayer = 0; // 0: laser artwork; 1/2: non-output T1/T2 helper shapes
    bool operator==(const Artwork &) const = default;
};
