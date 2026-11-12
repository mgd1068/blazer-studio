#include "job.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>

QByteArray JobPlan::motionFingerprint(const Cancellation &cancel) const {
    if (!valid() || moves.size() > maximumMoves || isCancelled(cancel))
        return {};
    QByteArray bytes;
    bytes.reserve(moves.size() * 57 + 4);
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::BigEndian);
    stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
    stream << qint32(moves.size());
    for (const auto &move : moves) {
        if (isCancelled(cancel))
            return {};
        stream << move.from.x() << move.from.y() << move.to.x() << move.to.y() << move.speed
               << move.power << qint32(move.layer) << move.rapid << qint32(move.slice);
    }
    if (isCancelled(cancel))
        return {};
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
ControllerProgram JobPlan::encodedProgram(const Cancellation &cancel) const {
    const auto fail = [&](QString error, bool cancelled = false) {
        return ControllerProgram::failure(m_deviceProfile, {}, std::move(error), cancelled);
    };
    if (isCancelled(cancel))
        return fail("Ausgabe abgebrochen.", true);
    if (!valid())
        return fail(errors.isEmpty() ? QString("Kein gültiger Bewegungsplan.") : errors.join("; "));
    const auto adapter = ControllerAdapters::find(m_deviceProfile);
    if (!adapter)
        return fail(ControllerAdapters::outputError(m_deviceProfile));
    const auto profileError = adapter->outputError(m_deviceProfile);
    if (!profileError.isEmpty())
        return fail(profileError);
    if (moves.size() > maximumMoves)
        return fail("Ausgabeplan überschreitet 500000 Segmente.");
    if (m_program && m_program->valid() && m_programMoves.size() == moves.size() &&
        m_programMoves.constData() == moves.constData() &&
        m_program->matches(m_deviceProfile, m_program->sourceFingerprint()))
        return *m_program;
    const auto source = motionFingerprint(cancel);
    if (isCancelled(cancel))
        return fail("Ausgabe abgebrochen.", true);
    if (source.isEmpty())
        return fail("Bewegungsplan konnte nicht gebunden werden.");
    if (m_program && m_program->valid() && m_program->matches(m_deviceProfile, source))
        return *m_program;
    auto program = adapter->encode(*this, source, cancel);
    if (isCancelled(cancel))
        return fail("Ausgabe abgebrochen.", true);
    if (program.valid() && !program.matches(m_deviceProfile, source))
        return fail("Ausgabeadapter lieferte einen abweichenden Auftragsvertrag.");
    return program;
}
QStringList JobPlan::gcode(const Cancellation &cancel) const {
    return encodedProgram(cancel).gcodeLines();
}
void JobPlan::cacheCommands(const Cancellation &cancel) {
    m_program = encodedProgram(cancel);
    m_programMoves = m_program->valid() ? moves.values() : QVector<JobMove>{};
}
QStringList JobPlan::cachedCommands() const {
    if (!valid() || !m_program || !m_program->valid())
        return {};
    const bool shared =
        m_programMoves.size() == moves.size() && m_programMoves.constData() == moves.constData();
    if (!m_program->matches(m_deviceProfile,
                            shared ? m_program->sourceFingerprint() : motionFingerprint()))
        return {};
    return m_program->gcodeLines();
}
