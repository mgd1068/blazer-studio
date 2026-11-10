#pragma once
#include "artwork.h"
#include "cancellation.h"
#include <QHash>
#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
struct JobInput {
    QList<Artwork> items;
    QVariantList layers;
    int activePage = 0;
};
class Document;
struct JobMove {
    QPointF from, to;
    double speed = 0, power = 0;
    int layer = 0;
    bool rapid = false;
    int slice = 0;
};
struct JobPlan {
    QVector<JobMove> moves;
    QHash<QString, QImage> previews;
    bool cancelled = false;
    QStringList errors;
    QStringList commands;
    double workLength = 0, travelLength = 0, seconds = 0, minimumSeconds = 0;
    bool valid() const { return errors.isEmpty() && !moves.isEmpty(); }
    QStringList gcode(const Cancellation &cancel = {}) const;
    QVariantMap summary() const;
};
class JobPlanner {
  public:
    static JobPlan build(const Document &document);
    static JobPlan build(const JobInput &input, const Cancellation &cancel = {});
    static JobPlan materialTest(double minSpeed, double maxSpeed, double minPower, double maxPower,
                                int steps);
    static QPainterPath geometry(const Artwork &item, QString &error, bool fill = false);
};
