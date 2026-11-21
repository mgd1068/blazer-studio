#pragma once
// Test-only plant, intentionally independent of the application encoder,
// planner, geometry helpers and coordinate/bounds checks. This is a kinematic
// model, not a firmware emulator or a mechanical/hardware acceptance claim.
#include <QByteArray>
#include <QPointF>
#include <QRectF>
#include <QRegularExpression>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <optional>

class VirtualGrblPlant {
  public:
    enum class Result { Accepted, Rejected, Limit };
    struct Travel {
        QPointF from, to, attempted;
        bool rapid = false, emitting = false;
    };
    struct Modal {
        bool absolute = true, inches = false;
        int motion = 0, coordinate = 54, spindle = 0;
        double power = 0;
    } modal;
    QRectF envelope;
    QPointF physical, counterBias, g54, g55, homePoint;
    QVector<Travel> travel;
    bool alarm = false, homed = false, hold = false;
    int resets = 0, homes = 0, limits = 0, rejected = 0;

    VirtualGrblPlant(QRectF bounds, QPointF actual, QPointF workOffset, QPointF home)
        : envelope(bounds), physical(actual), counterBias(-actual), g54(workOffset),
          homePoint(home) {}
    QPointF counter() const { return physical + counterBias; }
    QPointF offset() const { return modal.coordinate == 54 ? g54 : g55; }
    QPointF work() const { return counter() - offset(); }
    double idleOutput() const { return modal.spindle == 3 && !alarm ? modal.power : 0; }
    void reset() {
        // Deliberately model a possible lost-reference controller: its counters
        // restart at zero without changing physical head location. This is NOT
        // a claim about the actual M3 firmware's reset behavior.
        modal = {};
        counterBias = -physical;
        alarm = homed = hold = false;
        ++resets;
    }
    void home() {
        physical = homePoint;
        counterBias = {};
        alarm = hold = false;
        homed = true;
        modal.spindle = 0;
        modal.power = 0;
        ++homes;
    }
    Result apply(QByteArray block) {
        block = block.trimmed();
        if (block.size() > 256) {
            ++rejected;
            return Result::Rejected;
        }
        if (block == "$H") {
            home();
            return Result::Accepted;
        }
        if (block.isEmpty() || (block.startsWith('(') && block.endsWith(')')))
            return Result::Accepted;
        const bool jog = block.startsWith("$J=");
        if (jog)
            block.remove(0, 3);
        // Parse the entire block before changing any modal/power/position state.
        // No ignored suffixes, NaN/Inf, exponents, duplicate axis words, or
        // unsupported modal commands may partially change the physical model.
        struct Word {
            char letter;
            double value;
        };
        QVector<Word> words;
        static const QRegularExpression token("([A-Z])([+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+))");
        const auto text = QString::fromLatin1(block);
        qsizetype cursor = 0;
        auto reject = [&] {
            ++rejected;
            return Result::Rejected;
        };
        while (cursor < text.size()) {
            if (text[cursor].isSpace()) {
                ++cursor;
                continue;
            }
            const auto match = token.match(text, cursor, QRegularExpression::NormalMatch,
                                           QRegularExpression::AnchorAtOffsetMatchOption);
            if (!match.hasMatch())
                return reject();
            bool ok;
            const auto value = match.captured(2).toDouble(&ok);
            if (!ok || !std::isfinite(value))
                return reject();
            words.append({match.captured(1).toLatin1()[0], value});
            cursor = match.capturedEnd();
        }
        if (words.isEmpty())
            return reject();
        auto next = modal;
        std::optional<double> x, y, s, f, l, p, tool;
        bool motionSeen = false, unitsSeen = false, distanceSeen = false, coordinateSeen = false,
             spindleSeen = false, setup = false, dwell = false;
        auto set = [](std::optional<double> &field, double value) {
            if (field)
                return false;
            field = value;
            return true;
        };
        for (const auto &word : words) {
            if (word.letter == 'G') {
                if (word.value < 0 || word.value > 100 || std::floor(word.value) != word.value)
                    return reject();
                const auto code = int(word.value);
                switch (code) {
                case 0:
                case 1:
                    if (motionSeen)
                        return reject();
                    motionSeen = true;
                    next.motion = code;
                    break;
                case 20:
                case 21:
                    if (unitsSeen)
                        return reject();
                    unitsSeen = true;
                    next.inches = code == 20;
                    break;
                case 90:
                case 91:
                    if (distanceSeen)
                        return reject();
                    distanceSeen = true;
                    next.absolute = code == 90;
                    break;
                case 54:
                case 55:
                    if (coordinateSeen)
                        return reject();
                    coordinateSeen = true;
                    next.coordinate = code;
                    break;
                case 17:
                case 94:
                    break;
                case 10:
                    if (setup || dwell || motionSeen)
                        return reject();
                    setup = true;
                    break;
                case 4:
                    if (setup || dwell || motionSeen)
                        return reject();
                    dwell = true;
                    break;
                default:
                    return reject();
                }
            } else if (word.letter == 'M') {
                if (spindleSeen || (word.value != 3 && word.value != 4 && word.value != 5))
                    return reject();
                spindleSeen = true;
                next.spindle = word.value == 5 ? 0 : int(word.value);
            } else {
                bool valid = false;
                switch (word.letter) {
                case 'X':
                    valid = set(x, word.value);
                    break;
                case 'Y':
                    valid = set(y, word.value);
                    break;
                case 'S':
                    valid = set(s, word.value);
                    break;
                case 'F':
                    valid = set(f, word.value);
                    break;
                case 'L':
                    valid = set(l, word.value);
                    break;
                case 'P':
                    valid = set(p, word.value);
                    break;
                case 'T':
                    valid = set(tool, word.value);
                    break;
                default:
                    break;
                }
                if (!valid)
                    return reject();
            }
        }
        if ((s && (*s < 0 || *s > 1000)) || (f && (*f <= 0 || *f > 100000)) ||
            (tool && *tool != 0) || (motionSeen && (setup || dwell)))
            return reject();
        if (setup) {
            if (l != 20 || p != 1 || !(x || y) || jog || spindleSeen)
                return reject();
        } else if (l || (p && !dwell))
            return reject();
        if (dwell && (!p || *p < 0 || *p > 10 || x || y || jog))
            return reject();
        if (alarm && !(spindleSeen && next.spindle == 0 && !x && !y))
            return reject();
        if (s)
            next.power = *s;
        const double scale = next.inches ? 25.4 : 1;
        if (setup) {
            auto newOffset = g54;
            if (x)
                newOffset.setX(counter().x() - *x * scale);
            if (y)
                newOffset.setY(counter().y() - *y * scale);
            modal = next;
            g54 = newOffset;
            return Result::Accepted;
        }
        if (!(x || y)) {
            modal = next;
            return Result::Accepted;
        }
        QPointF target = counter();
        const auto activeOffset = next.coordinate == 54 ? g54 : g55;
        if (x)
            target.setX(next.absolute ? *x * scale + activeOffset.x() : target.x() + *x * scale);
        if (y)
            target.setY(next.absolute ? *y * scale + activeOffset.y() : target.y() + *y * scale);
        const auto requestedPhysical = target - counterBias;
        const bool rapid = !jog && next.motion == 0;
        const bool emitting =
            !jog && !rapid && next.spindle != 0 && next.power > 0 && requestedPhysical != physical;
        const auto origin = physical;
        constexpr double epsilon = 1e-8;
        const bool outside = requestedPhysical.x() < envelope.left() - epsilon ||
                             requestedPhysical.y() < envelope.top() - epsilon ||
                             requestedPhysical.x() > envelope.right() + epsilon ||
                             requestedPhysical.y() > envelope.bottom() + epsilon;
        if (outside) {
            // Follow the segment to its FIRST hard-boundary intersection.
            double fraction = 1;
            const auto delta = requestedPhysical - origin;
            auto clip = [&](double start, double d, double low, double high) {
                if (d > 0)
                    fraction = std::min(fraction, (high - start) / d);
                if (d < 0)
                    fraction = std::min(fraction, (low - start) / d);
            };
            clip(origin.x(), delta.x(), envelope.left(), envelope.right());
            clip(origin.y(), delta.y(), envelope.top(), envelope.bottom());
            physical = origin + delta * std::clamp(fraction, 0., 1.);
            travel.append({origin, physical, requestedPhysical, rapid, emitting});
            modal = next;
            modal.spindle = 0;
            modal.power = 0;
            alarm = true;
            ++limits;
            return Result::Limit;
        }
        physical = requestedPhysical;
        travel.append({origin, physical, requestedPhysical, rapid, emitting});
        if (!jog)
            modal = next; // GRBL $J does not contaminate persistent G modes.
        return Result::Accepted;
    }
};
