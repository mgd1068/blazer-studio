#pragma once
#include <QString>
struct Guide {
    QString id;
    QString axis;
    double position = 0;
    int page = 0;
    bool locked = false;
    bool operator==(const Guide &) const = default;
};
