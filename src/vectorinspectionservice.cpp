#include "vectorinspectionservice.h"
#include "document.h"
#include <QCryptographicHash>
#include <QtConcurrentRun>
VectorInspectionService::VectorInspectionService(Document *value, QObject *parent,
                                                 Inspector function)
    : QObject(parent), document(value), inspector(std::move(function)) {
    if (!inspector)
        inspector = VectorInspection::inspect;
    pool.setMaxThreadCount(1);
    connect(document, &Document::changed, this, [this] {
        // Generic planning/status notifications do not change authoring revision.
        // Never perform a geometry scan in this callback.
        if ((active || !published.isEmpty()) && document &&
            (document->historyRevision() != sourceRevision || !sourceSnapshot ||
             document->items().constData() != sourceSnapshot->items.constData() ||
             document->layers().constData() != sourceSnapshot->layers.constData() ||
             document->activePage() != sourceSnapshot->activePage ||
             document->deviceProfile().outputFingerprint() !=
                 sourceSnapshot->deviceProfile.outputFingerprint()))
            invalidate();
    });
    connect(document, &Document::selectionChanged, this, [this] {
        if (selectedOnly && (active || !published.isEmpty()) && selectionIds() != sourceSelection)
            invalidate();
    });
    connect(document, &QObject::destroyed, this, [this] { invalidate(); });
    connect(&watcher, &QFutureWatcher<VectorInspection::Report>::finished, this, [this] {
        active = false;
        if (!document || stale || isCancelled(cancellation) ||
            document->historyRevision() != sourceRevision ||
            QCryptographicHash::hash(document->projectBytes(), QCryptographicHash::Sha256) !=
                sourceHash ||
            (selectedOnly && selectionIds() != sourceSelection)) {
            published.clear();
            lastMessage = stale ? "Projekt geändert: erneut ausdrücklich prüfen."
                                : "Vektorprüfung abgebrochen; kein aktueller Prüfbericht.";
        } else {
            const auto result = watcher.result();
            published = result.toVariantMap();
            lastMessage =
                result.cancelled ? "Vektorprüfung abgebrochen."
                : result.complete
                    ? "Hinweisprüfung abgeschlossen. Kein Nachweis sicherer Ausgabe."
                    : "Vektorprüfung unvollständig: Arbeitsgrenzen oder Geometrie prüfen.";
        }
        emit changed();
    });
}
VectorInspectionService::~VectorInspectionService() {
    if (cancellation)
        cancellation->store(true, std::memory_order_relaxed);
    disconnect(&watcher, nullptr, this, nullptr);
    pool.waitForDone();
}
QStringList VectorInspectionService::selectionIds() const {
    QStringList ids;
    if (document)
        for (const auto &row : document->selectedRows()) {
            const int index = row.toInt();
            if (index >= 0 && index < document->items().size())
                ids.append(document->items()[index].id);
        }
    return ids;
}
void VectorInspectionService::invalidate() {
    stale = true;
    if (cancellation)
        cancellation->store(true, std::memory_order_relaxed);
    published.clear();
    lastMessage = "Projekt geändert: erneut ausdrücklich prüfen.";
    emit changed();
}
bool VectorInspectionService::inspect(bool selectionOnly) {
    if (active || !document)
        return false;
    sourceSelection = selectionOnly ? selectionIds() : QStringList{};
    if (selectionOnly && sourceSelection.isEmpty()) {
        published.clear();
        lastMessage = "Für die Auswahlprüfung zuerst Vektorobjekte auswählen.";
        emit changed();
        return false;
    }
    const auto snapshot = document->jobInput();
    sourceSnapshot = snapshot;
    sourceRevision = document->historyRevision();
    sourceHash = QCryptographicHash::hash(document->projectBytes(), QCryptographicHash::Sha256);
    selectedOnly = selectionOnly;
    stale = false;
    active = true;
    published.clear();
    cancellation = std::make_shared<std::atomic_bool>(false);
    lastMessage = "Vektorgeometrie wird begrenzt im Hintergrund geprüft.";
    const auto function = inspector;
    const auto token = cancellation;
    const auto ids = sourceSelection;
    watcher.setFuture(QtConcurrent::run(&pool, [snapshot, ids, token, function] {
        try {
            return function(snapshot, ids, token);
        } catch (...) {
            VectorInspection::Report result;
            result.complete = false;
            result.findings.append(
                {"geometry-unavailable",
                 "info",
                 "Vektorprüfung wegen Ressourcen- oder Geometriefehler unvollständig.",
                 {},
                 -1,
                 {}});
            return result;
        }
    }));
    emit changed();
    return true;
}
void VectorInspectionService::cancel() {
    if (cancellation)
        cancellation->store(true, std::memory_order_relaxed);
    published.clear();
    lastMessage = "Vektorprüfung abgebrochen; kein aktueller Prüfbericht.";
    emit changed();
}
