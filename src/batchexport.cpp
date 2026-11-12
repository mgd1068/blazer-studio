#include "batchexport.h"
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtConcurrentRun>
#include <exception>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
BatchExport::BatchExport(QObject *parent) : QObject(parent) {
    m_pool.setMaxThreadCount(1);
    connect(&m_watcher, &QFutureWatcher<BatchResult>::finished, this, [this] {
        const auto result = m_watcher.result();
        m_active = false;
        emit changed();
        emit finished(result.cancelled
                          ? "CSV-Export abgebrochen; veröffentlichte Dateien dieses Laufs entfernt."
                      : !result.error.isEmpty()
                          ? result.error
                          : QString::number(result.count) +
                                " Jobs exportiert; keine automatische Ausführung.");
    });
}
BatchExport::~BatchExport() {
    cancel();
    disconnect(&m_watcher, nullptr, this, nullptr);
    m_pool.waitForDone();
}
void BatchExport::cancel() {
    if (m_cancel)
        m_cancel->store(true, std::memory_order_relaxed);
}
bool BatchExport::start(JobInput input, QUrl csv, QUrl directory, QString pattern) {
    if (m_active)
        return false;
    m_active = true;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    auto token = m_cancel;
    m_watcher.setFuture(
        QtConcurrent::run(&m_pool, [input = std::move(input), csv, directory, pattern, token]() {
            try {
                return run(input, csv, directory, pattern, token);
            } catch (...) {
                return BatchResult{"CSV-Export fehlgeschlagen (Ressourcen- oder Datenfehler)."};
            }
        }));
    emit changed();
    return true;
}
BatchResult BatchExport::run(JobInput input, const QUrl &csv, const QUrl &directory,
                             const QString &pattern, const Cancellation &cancel) {
    if (!csv.isLocalFile() || !directory.isLocalFile() || pattern.size() > 1000)
        return {"CSV-Datei, Zielverzeichnis oder Tabellenformat ungültig."};
    QFile f(csv.toLocalFile());
    if (!f.open(QIODevice::ReadOnly) || f.size() > 1024 * 1024)
        return {"CSV-Datei, Zielverzeichnis oder Tabellenformat ungültig."};
    // RFC-style quoted fields, embedded newlines, escaped quotes.
    auto text = QString::fromUtf8(f.readAll());
    QChar delimiter = text.section('\n', 0, 0).contains(';') ? ';' : ',';
    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    for (int i = 0; i < text.size(); ++i) {
        QChar c = text[i];
        if (c == '"') {
            if (quoted && i + 1 < text.size() && text[i + 1] == '"') {
                field += '"';
                ++i;
            } else
                quoted = !quoted;
        } else if (!quoted && (c == delimiter || c == '\n')) {
            row << field;
            field.clear();
            if (c == '\n') {
                rows << row;
                row.clear();
            }
        } else if (c != '\r')
            field += c;
    }
    if (quoted)
        return {"CSV-Datei, Zielverzeichnis oder Tabellenformat ungültig."};
    if (!field.isEmpty() || !row.isEmpty()) {
        row << field;
        rows << row;
    }
    if (rows.size() < 2 || rows.size() > 1001)
        return {"CSV-Datei, Zielverzeichnis oder Tabellenformat ungültig."};
    auto headers = rows.takeFirst();
    if (headers.isEmpty())
        return {"CSV-Datei, Zielverzeichnis oder Tabellenformat ungültig."};
    QDir dir(directory.toLocalFile());
    if (!dir.exists())
        return {"CSV-Datei, Zielverzeichnis oder Tabellenformat ungültig."};

    QLockFile lock(dir.filePath(".blazer-batch.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return {"Ein anderer Export verwendet dieses Verzeichnis oder es ist nicht beschreibbar."};
    QTemporaryDir staging(dir.filePath(".blazer-batch-XXXXXX"));
    if (!staging.isValid())
        return {"Temporäres Exportverzeichnis konnte nicht angelegt werden."};
    QList<QPair<QString, QString>> files;
    qint64 stagedBytes = 0;
    for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        if (isCancelled(cancel))
            return {{}, 0, true};
        auto rowInput = input;
        for (auto &art : rowInput.items)
            if (art.page == input.activePage && art.kind == "text") {
                auto value = pattern.isEmpty() ? art.text : pattern;
                for (int col = 0; col < headers.size(); ++col)
                    value.replace("{" + headers[col] + "}", rows[rowIndex].value(col));
                value.replace("{number}", QString::number(rowIndex + 1));
                if (value.size() > 1000)
                    return {"CSV-Text überschreitet 1000 Zeichen."};
                art.text = value;
            }
        auto plan = JobPlanner::build(rowInput, cancel);
        if (isCancelled(cancel))
            return {{}, 0, true};
        if (!plan.valid())
            return {"CSV-Zeile " + QString::number(rowIndex + 1) + ": " + plan.errors.join("; ")};
        auto name = QString("job-%1.gcode").arg(rowIndex + 1, 4, 10, QChar('0'));
        auto destination = dir.filePath(name);
        if (QFile::exists(destination))
            return {"Stapelexport überschreibt keine vorhandenen Jobs."};
        const auto program = plan.encodedProgram(cancel);
        if (isCancelled(cancel))
            return {{}, 0, true};
        if (!program.valid() || program.mediaType() != "text/x-gcode")
            return {"CSV-Zeile " + QString::number(rowIndex + 1) + ": " +
                    (program.errors().isEmpty() ? QString("Kein GCode-Ausgabeadapter.")
                                                : program.errors().join("; "))};
        auto bytes = program.payload();
        stagedBytes += bytes.size();
        if (stagedBytes > 64 * 1024 * 1024)
            return {"CSV-Stapel überschreitet 64 MiB; kleinere Teilstapel verwenden."};
        auto temp = staging.filePath(name);
        QFile out(temp);
        if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.flush())
            return {"CSV-Auftrag konnte nicht vollständig vorbereitet werden."};
        out.close();
        files.append({temp, destination});
    }
    QStringList published;
    published.reserve(files.size());
    auto rollback = qScopeGuard([&] {
        for (const auto &path : published)
            QFile::remove(path);
    });
    for (const auto &entry : files) {
        // Linux atomic no-replace rename, including filesystems without hard links.
        // Staging is on the destination filesystem; existing files are preserved.
        if (isCancelled(cancel) ||
            ::syscall(SYS_renameat2, AT_FDCWD, QFile::encodeName(entry.first).constData(), AT_FDCWD,
                      QFile::encodeName(entry.second).constData(), RENAME_NOREPLACE) != 0) {
            return {isCancelled(cancel) ? QString()
                                        : QString("CSV-Datei konnte nicht veröffentlicht werden; "
                                                  "bestehende Dateien bleiben erhalten."),
                    0, isCancelled(cancel)};
        }
        published.append(entry.second);
    }
    if (isCancelled(cancel)) {
        return {{}, 0, true};
    }
    rollback.dismiss();
    return {{}, int(files.size()), false};
}
