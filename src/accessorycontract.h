#pragma once
#include <QString>
#include <QByteArray>
#include <atomic>
#include <limits>
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
    enum class SupervisionMode { SensorVerified, OperatorAttendedExternal, OperatorSupervisedDeviceCycle };
    enum class OperatorAction { StartJob, Focus, Resume };
    struct OperatorContext {
        QByteArray sessionBinding, operationBinding;
        OperatorAction action = OperatorAction::StartJob;
    };
    class OperatorWitness {
      public:
        qint64 witnessedAtMs() const { return m_witnessedAt; }
        qint64 expiresAtMs() const { return m_witnessedAt + operatorWitnessLifetimeMs; }
      private:
        friend class AccessoryContract;
        OperatorWitness(const AccessoryContract *issuer, quint64 identity, quint64 sequence,
                        OperatorContext context, qint64 witnessedAt)
            : m_issuer(issuer), m_identity(identity), m_sequence(sequence),
              m_context(std::move(context)), m_witnessedAt(witnessedAt) {}
        const AccessoryContract *m_issuer;
        quint64 m_identity, m_sequence;
        OperatorContext m_context;
        qint64 m_witnessedAt;
    };
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
    // Software freshness policy for a human statement, never a sensor sample.
    static constexpr qint64 operatorWitnessLifetimeMs = 30000;
    explicit AccessoryContract(bool extractionRequired = true)
        : m_extractionRequired(extractionRequired) {}

    SupervisionMode supervisionMode() const { return m_supervisionMode; }
    QString supervisionNotice() const {
        if (m_supervisionMode == SupervisionMode::OperatorSupervisedDeviceCycle)
            return QStringLiteral("Beaufsichtigter Gerätezyklus: Anwesenheit vor Start/Fokus/Fortsetzen bestätigen. Gehäuse- und Modullüfterzustand unbekannt; von Blazer nicht rücklesbar.");
        return m_supervisionMode == SupervisionMode::OperatorAttendedExternal
            ? QStringLiteral("Beaufsichtigte externe Absaugung: Bestätigung durch anwesende Bedienperson erforderlich. Luftstrom wird nicht gemessen.")
            : QStringLiteral("Sensorüberwachung erforderlich; aktueller Luftstrom siehe Zubehörstatus.");
    }
    bool selectOperatorAttendedExternal(const QByteArray &sessionBinding) {
        if (m_lease || m_releaseRequired || m_fault || !binding(sessionBinding)) return false;
        resetOperatorEvidence();
        m_supervisionMode = SupervisionMode::OperatorAttendedExternal;
        m_operatorSession = sessionBinding;
        m_states[index(Role::EnclosureExtraction)] = {};
        m_pending[index(Role::EnclosureExtraction)].reset();
        return true;
    }
    bool selectSensorVerified() {
        if (m_lease || m_releaseRequired || m_fault) return false;
        resetOperatorEvidence();
        m_operatorSession.clear();
        m_supervisionMode = SupervisionMode::SensorVerified;
        m_states[index(Role::EnclosureExtraction)] = {};
        m_pending[index(Role::EnclosureExtraction)].reset();
        return true;
    }
    std::optional<OperatorWitness> submitOperatorAttestation(
            const OperatorContext &context, bool externalExtractionOn, bool operatorPresent,
            qint64 witnessedAtMs, qint64 nowMs) {
        m_operatorPending.reset();
        if (m_supervisionMode != SupervisionMode::OperatorAttendedExternal || !externalExtractionOn)
            return {};
        return submitPresence(context, operatorPresent, witnessedAtMs, nowMs);
    }
    bool selectOperatorSupervisedDeviceCycle(const QByteArray &sessionBinding) {
        if (m_lease || m_releaseRequired || m_fault || !binding(sessionBinding)) return false;
        resetOperatorEvidence();
        m_supervisionMode = SupervisionMode::OperatorSupervisedDeviceCycle;
        m_operatorSession = sessionBinding;
        m_states[index(Role::EnclosureExtraction)] = {};
        m_pending[index(Role::EnclosureExtraction)].reset();
        return true;
    }
    std::optional<OperatorWitness> submitOperatorPresence(const OperatorContext &context,
            bool operatorPresent, qint64 witnessedAtMs, qint64 nowMs) {
        m_operatorPending.reset();
        if (m_supervisionMode != SupervisionMode::OperatorSupervisedDeviceCycle) return {};
        return submitPresence(context, operatorPresent, witnessedAtMs, nowMs);
    }
    QString operatorAdmissionError(const OperatorContext &context, const OperatorWitness &witness,
                                  qint64 nowMs, bool airAssistRequired = false) const {
        if (m_supervisionMode == SupervisionMode::SensorVerified)
            return QStringLiteral("Beaufsichtigter Bedienmodus ist nicht ausdrücklich gewählt.");
        if (m_fault)
            return QStringLiteral("Gerätezustand nach Störung unbekannt: sichere Freigabe und neue Bestätigung erforderlich.");
        if (!validContext(context) || context.sessionBinding != m_operatorSession)
            return QStringLiteral("Bestätigung gehört nicht zu dieser Session oder Ausgabeaktion.");
        if (!m_operatorPending || witness.m_issuer != this || witness.m_identity != m_issuerIdentity ||
            witness.m_sequence != m_operatorPending->m_sequence ||
            !sameContext(context, witness.m_context) || !sameContext(context, m_operatorPending->m_context) ||
            witness.m_witnessedAt != m_operatorPending->m_witnessedAt)
            return QStringLiteral("Frische einmalige Bestätigung für genau diese Ausgabeaktion erforderlich.");
        if (nowMs < witness.m_witnessedAt || nowMs - witness.m_witnessedAt > operatorWitnessLifetimeMs)
            return QStringLiteral("Menschliche Bedienbestätigung ist veraltet oder ihre Zeit ungültig.");
        if (m_lease) {
            if (!validOperatorLease() || !m_operatorPaused || context.action != OperatorAction::Resume ||
                m_operatorLease->action == OperatorAction::Focus ||
                context.operationBinding != m_operatorLease->operationBinding)
                return QStringLiteral("Resume benötigt den pausierten zugehörigen Auftrag und eine neue Bestätigung.");
        } else if (context.action == OperatorAction::Resume)
            return QStringLiteral("Kein zugehöriger pausierter Auftrag für Resume vorhanden.");
        return airAssistRequired ? roleError(Role::AirAssist, nowMs) : QString{};
    }
    bool beginOperator(const OperatorContext &context, const OperatorWitness &witness,
                       qint64 nowMs, bool airAssistRequired = false) {
        const auto error = operatorAdmissionError(context, witness, nowMs, airAssistRequired);
        m_operatorPending.reset(); // A submission attempt is one-shot, even when denied.
        if (!error.isEmpty()) return false;
        m_operatorLease = context;
        m_operatorLeaseIssuer = this;
        m_operatorAttending = true;
        m_operatorPaused = false;
        m_lease = true;
        m_releaseRequired = m_supervisionMode == SupervisionMode::OperatorAttendedExternal;
        return true;
    }
    void discardOperatorWitness() { m_operatorPending.reset(); }
    // Withdraw supervision if the operator leaves OR external extraction is lost.
    // This never turns an accessory off; the worker must suspend processing.
    void operatorNoLongerAttending() {
        if (m_supervisionMode != SupervisionMode::SensorVerified) transition(Event::Fault);
    }
    QString overridesError(qint64 nowMs) const {
        if (m_supervisionMode != SupervisionMode::SensorVerified)
            return QStringLiteral("Live-Overrides sind im beaufsichtigten Bedienmodus gesperrt.");
        return preflightError(nowMs);
    }

    bool configure(Role role, Mapping mapping) {
        if (m_lease || m_releaseRequired ||
            (role == Role::EnclosureExtraction && m_supervisionMode != SupervisionMode::SensorVerified))
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
        if (role == Role::EnclosureExtraction && m_supervisionMode != SupervisionMode::SensorVerified) return false;
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
        if (m_supervisionMode != SupervisionMode::SensorVerified) {
            if (!binding(m_operatorSession))
                return QStringLiteral("Beaufsichtigter Bedienmodus benötigt eine neue Sessionbindung.");
            if (m_lease) {
                if (!validOperatorLease() || !m_operatorAttending || nowMs < m_lastOperatorWitnessAt)
                    return QStringLiteral("Menschliche Beaufsichtigung ist nicht mehr gültig; Bearbeitung anhalten.");
            } else {
                if (!m_operatorPending || m_operatorPending->m_issuer != this ||
                    m_operatorPending->m_identity != m_issuerIdentity ||
                    nowMs < m_operatorPending->m_witnessedAt ||
                    nowMs - m_operatorPending->m_witnessedAt > operatorWitnessLifetimeMs)
                    return m_supervisionMode == SupervisionMode::OperatorSupervisedDeviceCycle
                        ? QStringLiteral("Eigene Anwesenheit vor Start/Fokus/Fortsetzen neu bestätigen; Lüfterzustände werden nicht rückgelesen.")
                        : QStringLiteral("Externe Absaugung und eigene Anwesenheit vor Start/Fokus/Resume neu bestätigen; Luftstrom nicht gemessen.");
            }
            return airAssistRequired ? roleError(Role::AirAssist, nowMs) : QString{};
        }
        if (m_extractionRequired) {
            const auto error = roleError(Role::EnclosureExtraction, nowMs);
            if (!error.isEmpty())
                return error;
        }
        return airAssistRequired ? roleError(Role::AirAssist, nowMs) : QString{};
    }
    bool begin(qint64 nowMs, bool airAssistRequired = false) {
        if (m_supervisionMode != SupervisionMode::SensorVerified) return false;
        if (!preflightError(nowMs, airAssistRequired).isEmpty())
            return false;
        m_lease = true;
        m_releaseRequired = m_extractionRequired;
        return true;
    }
    // Pause/resume must continue checking preflightError. Stop/completion do not
    // prove fumes have cleared; extraction OFF remains blocked until release.
    void transition(Event event) {
        m_operatorPending.reset();
        if (event == Event::Pause) m_operatorPaused = m_lease && validOperatorLease();
        else {
            m_operatorLease.reset();
            m_operatorLeaseIssuer = nullptr;
            m_operatorAttending = m_operatorPaused = false;
        }
        if (event == Event::Disconnect) m_operatorSession.clear();
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
        resetOperatorEvidence();
        return true;
    }
    bool releaseRequired() const { return m_releaseRequired; }
    bool activeLease() const { return m_lease; }
    bool faulted() const { return m_fault; }

  private:
    std::optional<OperatorWitness> submitPresence(const OperatorContext &context,
            bool operatorPresent, qint64 witnessedAtMs, qint64 nowMs) {
        if (m_fault || !validContext(context) || context.sessionBinding != m_operatorSession ||
            !operatorPresent || witnessedAtMs < 0 || nowMs < witnessedAtMs ||
            nowMs - witnessedAtMs > operatorWitnessLifetimeMs || witnessedAtMs < m_lastOperatorWitnessAt ||
            witnessedAtMs > std::numeric_limits<qint64>::max() - operatorWitnessLifetimeMs ||
            m_operatorSequence == std::numeric_limits<quint64>::max()) return {};
        if (m_lease && (!validOperatorLease() || !m_operatorPaused || context.action != OperatorAction::Resume ||
            context.operationBinding != m_operatorLease->operationBinding ||
            m_operatorLease->action == OperatorAction::Focus)) return {};
        if (!m_lease && context.action == OperatorAction::Resume) return {};
        m_lastOperatorWitnessAt = witnessedAtMs;
        OperatorWitness witness(this, m_issuerIdentity, ++m_operatorSequence, context, witnessedAtMs);
        m_operatorPending = witness;
        return witness;
    }
    static bool binding(const QByteArray &value) {
        if (value.size() != 64) return false;
        for (const char c : value) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        return true;
    }
    static bool validContext(const OperatorContext &context) {
        return binding(context.sessionBinding) && binding(context.operationBinding) &&
            (context.action == OperatorAction::StartJob || context.action == OperatorAction::Focus ||
             context.action == OperatorAction::Resume);
    }
    static bool sameContext(const OperatorContext &a, const OperatorContext &b) {
        return a.sessionBinding == b.sessionBinding && a.operationBinding == b.operationBinding && a.action == b.action;
    }
    bool validOperatorLease() const {
        return m_operatorLease && m_operatorLeaseIssuer == this &&
            m_operatorLease->sessionBinding == m_operatorSession;
    }
    void resetOperatorEvidence() {
        m_operatorPending.reset(); m_operatorLease.reset(); m_operatorLeaseIssuer = nullptr;
        m_operatorAttending = m_operatorPaused = false;
    }
    inline static std::atomic<quint64> s_issuerSequence{0};
    quint64 m_issuerIdentity = ++s_issuerSequence;
    SupervisionMode m_supervisionMode = SupervisionMode::SensorVerified;
    QByteArray m_operatorSession;
    std::optional<OperatorWitness> m_operatorPending;
    std::optional<OperatorContext> m_operatorLease;
    const AccessoryContract *m_operatorLeaseIssuer = nullptr;
    quint64 m_operatorSequence = 0;
    qint64 m_lastOperatorWitnessAt = -1;
    bool m_operatorAttending = false, m_operatorPaused = false;
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
