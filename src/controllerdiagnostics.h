#pragma once
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
#include <QVariantMap>
#include <array>
#include <cmath>
#include <deque>
#include <optional>

// Opt-in technical context only. Typed input prevents endpoint names, controller
// free text, raw GCode and project content from entering the retained buffer.
// Queued/acknowledged protocol bytes never prove physical motion or optical power.
class ControllerDiagnostics {
  public:
    static constexpr int maximumEvents = 64;
    static constexpr int maximumPending = 64;
    static constexpr int maximumSnapshotBytes = 32768;
    enum class Event {
        ActionRequested,
        PreflightDenied,
        CommandQueued,
        CommandAcknowledged,
        ResetRequested,
        ResetWriteAccepted,
        StartupObserved,
        ControllerError,
        ControllerAlarm,
        Fault,
        Disconnected,
        RealtimeWrite,
        CommandRejected
    };
    enum class Action { None, Start, Jog, Frame, Zero, Focus, Home, Dispatch };
    enum class Denial {
        None,
        MotionContract,
        ReferenceMissing,
        StaleStatus,
        ProfileMismatch,
        PhysicalBounds,
        InvalidPlan,
        OutputContract,
        ControllerNotReady,
        InvalidRegion,
        HomingContract,
        HomingPrerequisite
    };
    enum class Command {
        Unretained,
        SafetyOff,
        Identify,
        ReadSettings,
        ReadModal,
        Home,
        WorkZero,
        Jog,
        UnitsMm,
        Absolute,
        G54,
        PlaneXY,
        FeedMode,
        PowerZero,
        DynamicPowerRequested,
        ConstantPowerRequested,
        Dwell,
        FirstApproachRequested,
        FirstPoweredTargetRequested,
        FinalSafetyOff,
        FeedHold,
        CycleResume,
        Override
    };
    enum class FaultCause {
        Unspecified,
        Transport,
        Timeout,
        InvalidReply,
        UnsupportedController,
        WriteFailure,
        ControllerError,
        CoordinateMismatch,
        OffsetChanged,
        ControllerLocked,
        IdleOutputReported,
        ExcessOverride,
        UnexpectedAck,
        ConfigurationReadback
    };
    enum class ResetReason { None, Connect, Stop, Fault, Disconnect };
    enum class ControllerState { Unknown, Idle, Run, Home, Hold, Alarm, Door, Jog, Check, Sleep };
    enum class Coordinate { Work, Machine, Offset };
    struct LiveState {
        bool connected = false, ready = false, verified = false;
        bool positionKnown = false, offsetKnown = false, outputKnown = false;
        bool protocolReferenced = false, homingContract = false, envelopeContract = false,
             modalContract = false, statusFresh = false;
        qint64 statusAgeMs = -1;
        std::optional<double> reportedPower;
    };
    static constexpr std::array<int, 16> settingKeys{3,  5,  13, 20, 21, 22, 23,  24,
                                                     25, 26, 27, 30, 31, 32, 130, 131};

    ControllerDiagnostics() { clock.start(); }
    void request(Action action) { record(Event::ActionRequested, action); }
    void deny(Action action, Denial reason) { record(Event::PreflightDenied, action, reason); }
    void beginStream(bool job, LiveState state) {
        jobStream = job;
        firstApproach = firstPoweredTarget = finalOff = false;
        preambleRecords = 0;
        if (job) {
            startCoordinates = coordinates;
            startSettings = settings;
            startModalCodes = modalCodes;
            startModalTime = modalTime;
            startTime = clock.elapsed();
            startState = state;
        }
    }
    bool jobActive() const { return jobStream; }
    bool wantsApproach() const { return jobStream && !firstApproach; }
    bool wantsPoweredTarget() const { return jobStream && !firstPoweredTarget; }
    void queued(Command command, std::optional<QPointF> xy = {}, std::optional<double> power = {}) {
        ++dispatchCount;
        if (command == Command::FirstApproachRequested) {
            if (!jobStream || firstApproach)
                command = Command::Unretained;
            else
                firstApproach = true;
        }
        if (command == Command::FirstPoweredTargetRequested) {
            if (!jobStream || firstPoweredTarget)
                command = Command::Unretained;
            else
                firstPoweredTarget = true;
        }
        if (command == Command::FinalSafetyOff) {
            if (!jobStream || finalOff)
                command = Command::Unretained;
            else
                finalOff = true;
        }
        // Even an all-zero-power job cannot retain an unlimited apparent
        // preamble. Preserve only its first targets and final shutdown boundary.
        if (jobStream && command != Command::FirstApproachRequested &&
            command != Command::FirstPoweredTargetRequested && command != Command::FinalSafetyOff &&
            command != Command::Unretained) {
            if (firstPoweredTarget || preambleRecords >= 16)
                command = Command::Unretained;
            else
                ++preambleRecords;
        }
        if (command != Command::FirstApproachRequested &&
            command != Command::FirstPoweredTargetRequested)
            xy.reset();
        if (command != Command::DynamicPowerRequested &&
            command != Command::ConstantPowerRequested &&
            command != Command::FirstPoweredTargetRequested)
            power.reset();
        quint64 id = 0;
        if (command != Command::Unretained)
            id = record(Event::CommandQueued, Action::Dispatch, Denial::None, command, xy, 0, 0,
                        power);
        if (pending.size() == maximumPending) {
            for (auto &event : events)
                if (event.id == pending.front().id && event.ack == 0)
                    event.ack = 4;
            pending.pop_front();
            ++untrackedPending;
            ++pendingOverflowCount;
        }
        pending.push_back({id, command});
    }
    void acknowledged() {
        ++ackCount;
        if (untrackedPending) {
            --untrackedPending;
            return;
        }
        if (pending.empty()) {
            ++unmatchedCount;
            return;
        }
        const auto item = pending.front();
        pending.pop_front();
        if (!item.id)
            return;
        for (auto &event : events)
            if (event.id == item.id)
                event.ack = 1;
        record(Event::CommandAcknowledged, Action::Dispatch, Denial::None, item.command, {},
               item.id);
    }
    void rejected(int code) {
        ++rejectionCount;
        if (untrackedPending) {
            --untrackedPending;
            return;
        }
        if (pending.empty())
            return;
        const auto item = pending.front();
        pending.pop_front();
        if (!item.id)
            return;
        for (auto &event : events)
            if (event.id == item.id)
                event.ack = 3;
        record(Event::CommandRejected, Action::Dispatch, Denial::None, item.command, {}, item.id,
               code >= 0 && code <= 255 ? code : -1);
    }
    void unmatchedAcknowledgement() {
        ++ackCount;
        ++unmatchedCount;
    }
    void resetRequested(ResetReason reason) {
        ++resetCount;
        invalidatePending();
        jobStream = false;
        record(Event::ResetRequested, Action::None, Denial::None, Command::Unretained, {}, 0,
               int(reason));
    }
    void resetWriteAccepted(bool accepted) {
        record(Event::ResetWriteAccepted, Action::None, Denial::None, Command::Unretained, {}, 0,
               accepted ? 1 : 0);
    }
    void startupObserved() {
        ++startupCount;
        invalidatePending();
        jobStream = false;
        for (auto &setting : settings)
            setting.known = false;
        record(Event::StartupObserved);
    }
    void protocolFault(bool alarm, int code) {
        record(alarm ? Event::ControllerAlarm : Event::ControllerError, Action::None, Denial::None,
               Command::Unretained, {}, 0, code >= 0 && code <= 255 ? code : -1);
    }
    void fault(FaultCause cause) {
        ++faultCount;
        record(Event::Fault, Action::None, Denial::None, Command::Unretained, {}, 0, int(cause));
    }
    void reportedOutput(std::optional<double> value) {
        outputTime = clock.elapsed();
        outputKnown = value && numeric(*value) && *value >= 0;
        if (outputKnown)
            outputValue = *value;
    }
    void realtime(Command command, bool accepted) {
        ++realtimeCount;
        record(Event::RealtimeWrite, Action::Dispatch, Denial::None, command, {}, 0,
               accepted ? 1 : 0);
    }
    void statusQuery(bool accepted) {
        ++statusQueries;
        if (accepted)
            ++acceptedStatusQueries;
    }
    void disconnected() {
        invalidatePending();
        record(Event::Disconnected);
    }
    void coordinate(Coordinate kind, std::optional<QPointF> point) {
        if (size_t(kind) >= coordinates.size())
            return;
        auto &slot = coordinates[size_t(kind)];
        slot.observed = clock.elapsed();
        slot.known = point && numeric(point->x()) && numeric(point->y());
        if (slot.known)
            slot.value = *point;
    }
    void reportedModal(int coordinate, int units, int distance) {
        modalTime = clock.elapsed();
        modalCodes = {coordinate >= 54 && coordinate <= 59 ? coordinate : -1,
                      units == 20 || units == 21 ? units : -1,
                      distance == 90 || distance == 91 ? distance : -1};
    }
    void reportedState(ControllerState state) {
        controllerState = state;
        reportTime = clock.elapsed();
    }
    void setting(int key, std::optional<double> value) {
        for (size_t i = 0; i < settingKeys.size(); ++i)
            if (settingKeys[i] == key) {
                settings[i].known = value && numeric(*value);
                settings[i].observed = clock.elapsed();
                if (settings[i].known)
                    settings[i].value = *value;
                return;
            }
    }
    QVariantMap snapshot(const LiveState &live) const {
        const auto now = clock.elapsed();
        QVariantList chronology;
        for (const auto &event : events) {
            QVariantMap item{{"sequence", QVariant::fromValue(event.id)},
                             {"elapsedMs", event.time},
                             {"event", eventName(event.kind)}};
            if (event.action != Action::None)
                item["action"] = actionName(event.action);
            if (event.denial != Denial::None)
                item["reason"] = denialName(event.denial);
            if (event.command != Command::Unretained)
                item["command"] = commandName(event.command);
            if (event.kind == Event::CommandQueued)
                item["acknowledgement"] = event.ack == 1   ? "acknowledged"
                                          : event.ack == 2 ? "notAcknowledgedBeforeBoundary"
                                          : event.ack == 3 ? "rejectedByController"
                                          : event.ack == 4 ? "acknowledgementTrackingLost"
                                                           : "pending";
            if (event.related)
                item["dispatchSequence"] = QVariant::fromValue(event.related);
            if (event.point) {
                item["xMm"] = event.point->x();
                item["yMm"] = event.point->y();
            }
            if (event.power)
                item["requestedS"] = *event.power;
            if (event.kind == Event::Fault)
                item["reason"] = faultName(event.code);
            if (event.kind == Event::ResetRequested)
                item["reason"] = resetName(event.code);
            if (event.kind == Event::ResetWriteAccepted || event.kind == Event::RealtimeWrite)
                item["acceptedBySerialBuffer"] = event.code == 1;
            if (event.kind == Event::ControllerError || event.kind == Event::ControllerAlarm ||
                event.kind == Event::CommandRejected)
                item["code"] = event.code;
            chronology.append(item);
        }
        QVariantMap positions;
        constexpr std::array<const char *, 3> coordinateNames{"workXY", "machineXY", "wcoXY"};
        for (size_t i = 0; i < coordinates.size(); ++i) {
            const auto &point = coordinates[i];
            QVariantMap item{{"known", point.known},
                             {"fresh", live.connected && live.statusFresh && point.known &&
                                           point.observed >= 0 && now - point.observed <= 1500},
                             {"ageMs", point.observed < 0 ? -1 : now - point.observed}};
            if (point.known) {
                item["xMm"] = point.value.x();
                item["yMm"] = point.value.y();
            }
            positions[coordinateNames[i]] = item;
        }
        QVariantMap startContext;
        if (startTime >= 0) {
            startContext = {{"elapsedMs", startTime},
                            {"homingProtocolReferenced", startState.protocolReferenced},
                            {"homingContract", startState.homingContract},
                            {"machineEnvelopeContract", startState.envelopeContract},
                            {"modalContract", startState.modalContract},
                            {"statusFresh", startState.statusFresh}};
            for (size_t i = 0; i < startCoordinates.size(); ++i) {
                const auto &point = startCoordinates[i];
                QVariantMap value{
                    {"known", point.known},
                    {"ageAtStartMs", point.observed < 0 ? -1 : startTime - point.observed}};
                if (point.known) {
                    value["xMm"] = point.value.x();
                    value["yMm"] = point.value.y();
                }
                startContext[coordinateNames[i]] = value;
            }
        }
        if (startTime >= 0) {
            QVariantMap readbackAtStart;
            for (size_t i = 0; i < startSettings.size(); ++i) {
                const auto &setting = startSettings[i];
                QVariantMap value{
                    {"known", setting.known},
                    {"ageAtStartMs", setting.observed < 0 ? -1 : startTime - setting.observed}};
                if (setting.known)
                    value["value"] = setting.value;
                readbackAtStart["$" + QString::number(settingKeys[i])] = value;
            }
            startContext["readback"] = readbackAtStart;
        }
        QVariantMap readback;
        for (size_t i = 0; i < settings.size(); ++i) {
            const auto &setting = settings[i];
            QVariantMap value{{"known", setting.known},
                              {"ageMs", setting.observed < 0 ? -1 : now - setting.observed}};
            if (setting.known)
                value["value"] = setting.value;
            readback["$" + QString::number(settingKeys[i])] = value;
        }
        QVariantMap state{{"controllerState", stateName(controllerState)},
                          {"connected", live.connected},
                          {"ready", live.ready},
                          {"verified", live.verified},
                          {"positionKnown", live.positionKnown},
                          {"offsetKnown", live.offsetKnown},
                          {"outputKnown", live.outputKnown},
                          {"statusFresh", live.statusFresh},
                          {"validStatusAgeMs", live.statusAgeMs},
                          {"lastReportAgeMs", reportTime < 0 ? -1 : now - reportTime},
                          {"homingProtocolReferenced", live.protocolReferenced},
                          {"homingContract", live.homingContract},
                          {"machineEnvelopeContract", live.envelopeContract},
                          {"modalContract", live.modalContract}};
        if (live.reportedPower && numeric(*live.reportedPower))
            state["controllerReportedOutput"] = *live.reportedPower;
        auto modalSnapshot = [](const std::array<int, 3> &codes, qint64 observed, qint64 at) {
            constexpr const char *keys[]{"workCoordinateCode", "unitsCode", "distanceCode"};
            QVariantMap result{{"ageMs", observed < 0 ? -1 : at - observed}};
            for (size_t i = 0; i < codes.size(); ++i) {
                QVariantMap value{{"known", codes[i] >= 0}};
                if (codes[i] >= 0)
                    value["value"] = codes[i];
                result[keys[i]] = value;
            }
            return result;
        };
        if (startTime >= 0)
            startContext["modalReadback"] =
                modalSnapshot(startModalCodes, startModalTime, startTime);
        QVariantMap outputReadback{{"known", outputKnown},
                                   {"ageMs", outputTime < 0 ? -1 : now - outputTime},
                                   {"fresh", outputKnown && live.statusFresh && outputTime >= 0 &&
                                                 now - outputTime <= 1500}};
        if (outputKnown)
            outputReadback["value"] = outputValue;
        QVariantMap result{
            {"supported", true},
            {"schemaVersion", 1},
            {"elapsedMs", now},
            {"maximumEvents", maximumEvents},
            {"maximumSnapshotBytes", maximumSnapshotBytes},
            {"droppedEvents", QVariant::fromValue(dropped)},
            {"events", chronology},
            {"positions", positions},
            {"streamStartContext", startContext},
            {"readback", readback},
            {"controllerOutputReadback", outputReadback},
            {"modalReadback", modalSnapshot(modalCodes, modalTime, now)},
            {"state", state},
            {"counters",
             QVariantMap{
                 {"commandBlocksQueued", QVariant::fromValue(dispatchCount)},
                 {"acknowledgementsReceived", QVariant::fromValue(ackCount)},
                 {"commandRejectionsReceived", QVariant::fromValue(rejectionCount)},
                 {"resetRequests", QVariant::fromValue(resetCount)},
                 {"startupBannersObserved", QVariant::fromValue(startupCount)},
                 {"faults", QVariant::fromValue(faultCount)},
                 {"unmatchedAcknowledgements", QVariant::fromValue(unmatchedCount)},
                 {"pendingTrackingOverflows", QVariant::fromValue(pendingOverflowCount)},
                 {"realtimeWriteRequests", QVariant::fromValue(realtimeCount)},
                 {"statusQueriesAttempted", QVariant::fromValue(statusQueries)},
                 {"statusQueriesAcceptedByBuffer", QVariant::fromValue(acceptedStatusQueries)}}}};
        // Defense in depth: future fields cannot silently break the export cap.
        while (!chronology.isEmpty() && QJsonDocument(QJsonObject::fromVariantMap(result))
                                                .toJson(QJsonDocument::Compact)
                                                .size() > maximumSnapshotBytes) {
            chronology.removeFirst();
            result["events"] = chronology;
            result["droppedEvents"] = result["droppedEvents"].toULongLong() + 1;
        }
        return result;
    }

  private:
    struct Entry {
        quint64 id;
        qint64 time;
        Event kind;
        Action action;
        Denial denial;
        Command command;
        std::optional<QPointF> point;
        quint64 related;
        int code;
        int ack = 0;
        std::optional<double> power;
    };
    struct Pending {
        quint64 id;
        Command command;
    };
    struct Point {
        bool known = false;
        QPointF value;
        qint64 observed = -1;
    };
    struct Setting {
        bool known = false;
        double value = 0;
        qint64 observed = -1;
    };
    QElapsedTimer clock;
    std::deque<Entry> events;
    std::deque<Pending> pending;
    std::array<Point, 3> coordinates, startCoordinates;
    LiveState startState;
    qint64 startTime = -1, modalTime = -1, startModalTime = -1;
    std::array<int, 3> modalCodes{-1, -1, -1}, startModalCodes{-1, -1, -1};
    std::array<Setting, settingKeys.size()> settings, startSettings;
    quint64 sequence = 0, dropped = 0, dispatchCount = 0, ackCount = 0, resetCount = 0,
            startupCount = 0, faultCount = 0, unmatchedCount = 0;
    quint64 rejectionCount = 0;
    quint64 untrackedPending = 0, pendingOverflowCount = 0, realtimeCount = 0, statusQueries = 0,
            acceptedStatusQueries = 0;
    bool outputKnown = false;
    double outputValue = 0;
    qint64 outputTime = -1;
    qint64 reportTime = -1;
    bool jobStream = false, firstApproach = false, firstPoweredTarget = false, finalOff = false;
    int preambleRecords = 0;
    ControllerState controllerState = ControllerState::Unknown;
    static bool numeric(double value) { return std::isfinite(value) && std::abs(value) <= 1e9; }
    quint64 record(Event kind, Action action = Action::None, Denial denial = Denial::None,
                   Command command = Command::Unretained, std::optional<QPointF> point = {},
                   quint64 related = 0, int code = 0, std::optional<double> power = {}) {
        if (point && (!numeric(point->x()) || !numeric(point->y())))
            point.reset();
        if (power && (!numeric(*power) || *power < 0))
            power.reset();
        if (events.size() == maximumEvents) {
            events.pop_front();
            ++dropped;
        }
        events.push_back({++sequence, clock.elapsed(), kind, action, denial, command, point,
                          related, code, 0, power});
        return sequence;
    }
    void invalidatePending() {
        for (auto &event : events)
            if (event.kind == Event::CommandQueued && event.ack == 0)
                event.ack = 2;
        pending.clear();
        untrackedPending = 0;
    }
    static QString eventName(Event value) {
        static constexpr const char *names[]{"actionRequested", "preflightDenied",
                                             "commandQueued",   "commandAcknowledged",
                                             "resetRequested",  "resetWriteAccepted",
                                             "startupObserved", "controllerError",
                                             "controllerAlarm", "fault",
                                             "disconnected",    "realtimeWrite",
                                             "commandRejected"};
        const auto index = size_t(value);
        return index < sizeof(names) / sizeof(*names) ? names[index] : "unknown";
    }
    static QString actionName(Action value) {
        static constexpr const char *names[]{"none", "start", "jog",  "frame",
                                             "zero", "focus", "home", "dispatch"};
        const auto index = size_t(value);
        return index < sizeof(names) / sizeof(*names) ? names[index] : "unknown";
    }
    static QString denialName(Denial value) {
        static constexpr const char *names[]{
            "none",          "motionContract",  "referenceMissing",
            "staleStatus",   "profileMismatch", "physicalBounds",
            "invalidPlan",   "outputContract",  "controllerNotReady",
            "invalidRegion", "homingContract",  "homingPrerequisite"};
        const auto index = size_t(value);
        return index < sizeof(names) / sizeof(*names) ? names[index] : "unknown";
    }
    static QString commandName(Command value) {
        static constexpr const char *names[]{"unretained",
                                             "safetyOffRequested",
                                             "identityReadRequested",
                                             "settingsReadRequested",
                                             "modalReadRequested",
                                             "homingRequested",
                                             "workZeroRequested",
                                             "jogRequested",
                                             "unitsMmRequested",
                                             "absoluteModeRequested",
                                             "g54Requested",
                                             "xyPlaneRequested",
                                             "feedModeRequested",
                                             "powerZeroRequested",
                                             "dynamicPowerRequested",
                                             "constantPowerRequested",
                                             "dwellRequested",
                                             "firstApproachRequested",
                                             "firstPoweredTargetRequested",
                                             "finalSafetyOffRequested",
                                             "feedHoldRequested",
                                             "cycleResumeRequested",
                                             "overrideRequested"};
        const auto index = size_t(value);
        return index < sizeof(names) / sizeof(*names) ? names[index] : "unknown";
    }
    static QString faultName(int value) {
        static constexpr const char *names[]{"unspecified",
                                             "transport",
                                             "timeout",
                                             "invalidReply",
                                             "unsupportedController",
                                             "writeFailure",
                                             "controllerError",
                                             "coordinateMismatch",
                                             "offsetChanged",
                                             "controllerLocked",
                                             "idleOutputReported",
                                             "excessOverride",
                                             "unexpectedAcknowledgement",
                                             "configurationReadback"};
        return value >= 0 && size_t(value) < sizeof(names) / sizeof(*names) ? names[value]
                                                                            : "unknown";
    }
    static QString resetName(int value) {
        static constexpr const char *names[]{"none", "connect", "stop", "fault", "disconnect"};
        return value >= 0 && value < 5 ? names[value] : "unknown";
    }
    static QString stateName(ControllerState value) {
        static constexpr const char *names[]{"unknown", "Idle", "Run", "Home",  "Hold",
                                             "Alarm",   "Door", "Jog", "Check", "Sleep"};
        const auto index = size_t(value);
        return index < sizeof(names) / sizeof(*names) ? names[index] : "unknown";
    }
};
