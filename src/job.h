#pragma once
#include <QPainterPath>
#include <QPointF>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
struct Artwork;
class Document;
struct JobMove {
    QPointF from, to;
    double speed = 0, power = 0;
    int layer = 0;
    bool rapid = false;
};
struct JobPlan {
    QVector<JobMove> moves;
    QStringList errors;
    double workLength = 0, travelLength = 0, seconds = 0;
    bool valid() const { return errors.isEmpty() && !moves.isEmpty(); }
    QStringList gcode() const;
    QVariantMap summary() const;
};
class JobPlanner {
  public:
    static JobPlan build(const Document &document);
    static JobPlan materialTest(double minSpeed, double maxSpeed, double minPower, double maxPower,
                                int steps);
    static QPainterPath geometry(const Artwork &item, QString &error, bool fill = false);
};
