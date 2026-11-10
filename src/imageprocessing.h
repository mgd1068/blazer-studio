#pragma once
#include "cancellation.h"
#include <QImage>
#include <QVariantMap>
namespace ImageProcessing {
bool validate(const QSize &source, const QVariantMap &settings, QString &error);
QImage apply(const QImage &source, const QVariantMap &settings, QString &error,
             const Cancellation &cancel = {});
} // namespace ImageProcessing
