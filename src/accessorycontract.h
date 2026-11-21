#pragma once
#include <QString>
#include <array>
#include <optional>
#include <utility>

// Transport-independent safety state. This class never opens a port or emits
// controller commands. A backend must resolve an explicitly verified output ID.
class AccessoryContract {
  public:
    enum class Role { EnclosureExtraction, AirAssist };
    enum class PhysicalState { Unknown, Flowing, NotFlowing };
    enum class CommandState { Unknown, Pending, AcknowledgedOn, AcknowledgedOff, Rejected };
    enum class Event { Pause, Complete, Stop, Fault, Disconnect };
    struct Mapping {
        QString outputId;
        QString verificationEvidence;
        bool verified = false;
    };
    struct Request {
        quint64 sequence;
        Role role;
        QString outputId;
        bool enabled;
    };
    struct State {
        CommandState command = CommandState::Unknown;
        PhysicalState physical = PhysicalState::Unknown;
        QString physicalEvidence;
        qint64 observedAtMs = -1;
        bool requestedOn = false;
    };
    // Uses a caller-supplied monotonic clock; wall-clock time must not be used.
    static constexpr qint64 observationLifetimeMs = 3000;
    explicit AccessoryContract(bool extractionRequired = true)
        : m_extractionRequired(extractionRequired) {}

    bool configure(Role role, Mapping mapping) {
        if (m_lease || m_releaseRequired)
            return false;
        const auto other = index(role) == 0 ? 1u : 0u;
        if (mapping.outputId.trimmed().isEmpty() ||
            (mapping.verified && mapping.verificationEvidence.trimmed().isEmpty()) ||
            mapping.outputId == m_mappings[other].outputId)
            return false;
        m_mappings[index(role)] = std::move(mapping);
        m_states[index(role)] = {};
        m_pending[index(role)].reset();
        return true;
    }
    const State &state(Role role) const { return m_states[index(role)]; }
    bool verified(Role role) const {
        const auto &mapping = m_mappings[index(role)];
        return mapping.verified && !mapping.outputId.trimmed().isEmpty() &&
               !mapping.verificationEvidence.trimmed().isEmpty();
    }
    std::optional<Request> request(Role role, bool enabled) {
        if (!verified(role) ||
            (!enabled && role == Role::EnclosureExtraction && (m_lease || m_releaseRequired)))
            return {};
        auto &state = m_states[index(role)];
        state.requestedOn = enabled;
        if (role == Role::EnclosureExtraction && enabled)
            m_releaseRequired = true;
        state.command = CommandState::Pending;
        // A new command invalidates prior physical evidence, including old ON.
        state.physical = PhysicalState::Unknown;
        state.observedAtMs = -1;
        state.physicalEvidence.clear();
        Request result{++m_sequence, role, m_mappings[index(role)].outputId, enabled};
        m_pending[index(role)] = result;
        return result;
    }
    bool acknowledge(const Request &request, bool accepted) {
        auto &pending = m_pending[index(request.role)];
        if (!pending || pending->sequence != request.sequence ||
            pending->outputId != request.outputId || pending->enabled != request.enabled)
            return false;
        m_states[index(request.role)].command =
            accepted
                ? (request.enabled ? CommandState::AcknowledgedOn : CommandState::AcknowledgedOff)
                : CommandState::Rejected;
        pending.reset();
        return true; // ACK does not establish actual airflow.
    }
    bool observe(Role role, PhysicalState physical, QString evidence, qint64 observedAtMs,
                 qint64 nowMs) {
        if (!verified(role) || observedAtMs < 0 || observedAtMs > nowMs ||
            observedAtMs < state(role).observedAtMs || evidence.trimmed().isEmpty())
            return false;
        auto &state = m_states[index(role)];
        state.physical = physical;
        state.physicalEvidence = std::move(evidence);
        state.observedAtMs = observedAtMs;
        return true;
    }
    QString preflightError(qint64 nowMs, bool airAssistRequired = false) const {
        if (m_fault)
            return QStringLiteral(
                "Zubehörzustand nach Störung unbekannt: sichere Freigabe erforderlich.");
        if (m_extractionRequired) {
            const auto error = roleError(Role::EnclosureExtraction, nowMs);
            if (!error.isEmpty())
                return error;
        }
        return airAssistRequired ? roleError(Role::AirAssist, nowMs) : QString{};
    }
    bool begin(qint64 nowMs, bool airAssistRequired = false) {
        if (!preflightError(nowMs, airAssistRequired).isEmpty())
            return false;
        m_lease = true;
        m_releaseRequired = m_extractionRequired;
        return true;
    }
    // Pause/resume must continue checking preflightError. Stop/completion do not
    // prove fumes have cleared; extraction OFF remains blocked until release.
    void transition(Event event) {
        if (event != Event::Pause)
            m_lease = false;
        if (event == Event::Fault || event == Event::Disconnect) {
            m_fault = true;
            for (auto &state : m_states)
                state = {};
            for (auto &pending : m_pending)
                pending.reset();
        }
    }
    // Caller must obtain explicit safe-clearance confirmation; never automatic
    // on job end. This grants no emission permission and sends no OFF request.
    bool releaseAfterSafeClearance(bool confirmed) {
        if (!confirmed || m_lease)
            return false;
        m_releaseRequired = false;
        m_fault = false;
        return true;
    }
    bool releaseRequired() const { return m_releaseRequired; }
    bool activeLease() const { return m_lease; }

  private:
    static constexpr size_t index(Role role) { return role == Role::EnclosureExtraction ? 0 : 1; }
    QString roleError(Role role, qint64 nowMs) const {
        const QString label = role == Role::EnclosureExtraction ? QStringLiteral("Gehäuseabsaugung")
                                                                : QStringLiteral("Air Assist");
        if (!verified(role))
            return label +
                   QStringLiteral(": Ausgangszuordnung ungeprüft; Verifikation erforderlich.");
        const auto &value = state(role);
        if (!value.requestedOn || value.command != CommandState::AcknowledgedOn)
            return label + QStringLiteral(": Einschaltanforderung nicht bestätigt.");
        if (value.physical != PhysicalState::Flowing || value.observedAtMs < 0 ||
            nowMs < value.observedAtMs || nowMs - value.observedAtMs > observationLifetimeMs)
            return label + QStringLiteral(": aktueller Luftstrom unbekannt oder nicht vorhanden; "
                                          "ACK ist kein Luftstromnachweis.");
        return {};
    }
    std::array<Mapping, 2> m_mappings{};
    std::array<State, 2> m_states{};
    std::array<std::optional<Request>, 2> m_pending{};
    quint64 m_sequence = 0;
    bool m_extractionRequired, m_lease = false, m_releaseRequired = false, m_fault = false;
};
