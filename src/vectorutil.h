#pragma once
#include <QJsonArray>
#include <QPainterPath>
namespace VectorUtil {
QJsonArray encode(const QPainterPath &path);
bool decode(const QJsonArray &data, QPainterPath &path);
QString svg(const QPainterPath &path);
} // namespace VectorUtil
