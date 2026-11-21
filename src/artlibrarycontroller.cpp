#include "artlibrarycontroller.h"
#include "artlibrary.h"
#include "document.h"
#include "machine.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QScopedValueRollback>
#include <QSet>
#include <QUuid>
#include <algorithm>
namespace {
QString imageSource(const QByteArray &png) {
    return png.isEmpty() ? QString{}
                         : "data:image/png;base64," + QString::fromLatin1(png.toBase64());
}
QByteArray exactMap(const QVariantMap &value) {
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << value;
    return bytes;
}
bool exactArtwork(const Artwork &a, const Artwork &b) {
    if (a.id != b.id || a.name != b.name || a.kind != b.kind || a.text != b.text ||
        a.layer != b.layer || a.visible != b.visible || a.groupId != b.groupId ||
        a.groupName != b.groupName || a.extraOperations != b.extraOperations || a.page != b.page ||
        a.toolLayer != b.toolLayer || a.engraveOutline != b.engraveOutline ||
        a.relief != b.relief || a.bounds.x() != b.bounds.x() || a.bounds.y() != b.bounds.y() ||
        a.bounds.width() != b.bounds.width() || a.bounds.height() != b.bounds.height() ||
        exactMap(a.style) != exactMap(b.style) || exactMap(a.settings) != exactMap(b.settings))
        return false;
    if (a.kind == "path") {
        if (a.path.fillRule() != b.path.fillRule() ||
            a.path.elementCount() != b.path.elementCount())
            return false;
        for (int i = 0; i < a.path.elementCount(); ++i) {
            const auto first = a.path.elementAt(i), second = b.path.elementAt(i);
            if (first.type != second.type || first.x != second.x || first.y != second.y)
                return false;
        }
    }
    if (a.asset != b.asset && (!a.asset || !b.asset || a.asset->bytes != b.asset->bytes))
        return false;
    if (a.reliefAsset != b.reliefAsset &&
        (!a.reliefAsset || !b.reliefAsset || a.reliefAsset->png != b.reliefAsset->png))
        return false;
    return true;
}
bool tokenSyntax(const QString &token) {
    return token.size() == 64 && std::all_of(token.cbegin(), token.cend(), [](QChar c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
} // namespace
ArtLibraryController::ArtLibraryController(Document *d, Machine *m, ArtLibrary *l, QObject *parent)
    : QObject(parent), document(d), machine(m), library(l) {
    const auto sourceChanged = [this] {
        if (!frozenGeometry.isEmpty() && !sourceMatches())
            invalidateSource();
        else
            emit changed();
    };
    if (document) {
        connect(document, &Document::changed, this, sourceChanged);
        connect(document, &Document::selectionChanged, this, sourceChanged);
        connect(document, &Document::destroyed, this, [this] { invalidateSource(); });
    }
    if (machine) {
        connect(machine, &Simulator::changed, this, &ArtLibraryController::changed);
        connect(machine, &QObject::destroyed, this, &ArtLibraryController::changed);
    }
    if (library) {
        connect(library, &ArtLibrary::changed, this, [this] {
            if (library) {
                QSet<QString> existing;
                for (const auto &entry : library->entries())
                    existing.insert(entry.toMap()["id"].toString());
                for (const auto &id : thumbnails.keys())
                    if (!existing.contains(id))
                        thumbnails.remove(id);
            } else
                thumbnails.clear();
            emit changed();
        });
        connect(library, &QObject::destroyed, this, [this] {
            thumbnails.clear();
            emit changed();
        });
    }
}
QString ArtLibraryController::blockReason() const {
    if (mutating)
        return "Bibliotheksaktion läuft; bitte warten.";
    if (!document || !machine || !library)
        return "Grafikbibliothek oder Dokument ist nicht verfügbar.";
    if (!library->writable())
        return library->message();
    if (machine->busy() || machine->firing())
        return "Auftrag oder Fokus zuerst beenden.";
    if (document->planningBusy() || document->batchBusy() || document->imageExportBusy())
        return "Berechnung oder Export zuerst beenden.";
    return {};
}
bool ArtLibraryController::fail(QString message) {
    if (signalingFailure)
        return false;
    QScopedValueRollback<bool> guard(signalingFailure, true);
    lastMessage = std::move(message);
    emit changed();
    return false;
}
QList<Artwork> ArtLibraryController::selection(QString &error) const {
    QList<Artwork> selected;
    if (!document) {
        error = "Quelldokument nicht verfügbar.";
        return {};
    }
    QSet<int> seen;
    for (const auto &value : document->selectedRows()) {
        const int row = value.toInt();
        if (row < 0 || row >= document->items().size() || seen.contains(row)) {
            error = "Auswahl ist inkonsistent.";
            return {};
        }
        seen.insert(row);
        const auto &art = document->items()[row];
        if (!art.visible || art.page != document->activePage()) {
            error = "Nur sichtbare Auswahl auf dem aktiven Blatt speichern.";
            return {};
        }
        selected.append(art);
    }
    if (selected.isEmpty())
        error = "Zuerst sichtbare Grafikobjekte auswählen.";
    return selected;
}
bool ArtLibraryController::sourceMatches() const {
    if (!document || sourcePage != document->activePage() || sourceItems.isEmpty())
        return false;
    const auto rows = document->selectedRows();
    if (rows.size() != sourceItems.size())
        return false;
    for (int i = 0; i < rows.size(); ++i) {
        const int row = rows[i].toInt();
        if (row < 0 || row >= document->items().size() ||
            !exactArtwork(document->items()[row], sourceItems[i]))
            return false;
    }
    return true;
}
void ArtLibraryController::invalidateSource() {
    frozenGeometry.clear();
    sourceItems.clear();
    sourcePage = -1;
    token.clear();
    currentReview.clear();
    lastMessage = "Auswahl oder Quelldaten geändert; erneut ausdrücklich prüfen.";
    emit changed();
}
QVariantMap ArtLibraryController::reviewSelection() {
    if (mutating || signalingFailure)
        return {};
    const auto reason = blockReason();
    Mutation mutation(*this);
    frozenGeometry.clear();
    sourceItems.clear();
    sourcePage = -1;
    token.clear();
    currentReview.clear();
    if (!reason.isEmpty()) {
        fail(reason);
        currentReview = {{"valid", false}, {"error", reason}};
        return currentReview;
    }
    QString error;
    auto selected = selection(error);
    if (!error.isEmpty()) {
        fail(error);
        currentReview = {{"valid", false}, {"error", error}};
        return currentReview;
    }
    auto encoded = GeometryAssetCodec::encode(selected, true);
    if (!encoded.valid()) {
        fail(encoded.error);
        currentReview = {
            {"valid", false}, {"error", encoded.error}, {"issues", encoded.review.issues}};
        return currentReview;
    }
    const auto decoded = GeometryAssetCodec::decode(encoded.bytes);
    if (!decoded.valid()) {
        fail(decoded.error);
        return {{"valid", false}, {"error", decoded.error}};
    }
    sourceItems = std::move(selected);
    sourcePage = document->activePage();
    frozenGeometry = std::move(encoded.bytes);
    token = QString::fromLatin1(
        QCryptographicHash::hash(QUuid::createUuid().toRfc4122() + encoded.contentHash,
                                 QCryptographicHash::Sha256)
            .toHex());
    QString thumbnailError;
    const auto png = GeometryAssetCodec::thumbnail(frozenGeometry, thumbnailError);
    auto issues = encoded.review.issues;
    if (png.isEmpty())
        issues.append(QVariantMap{
            {"code", "preview-unavailable"},
            {"label", "Begrenzte Konturvorschau nicht verfügbar; Geometrie wird nicht verkürzt."}});
    currentReview = {{"valid", true},
                     {"error", QString()},
                     {"issues", issues},
                     {"conversionRequired", encoded.review.conversionRequired},
                     {"token", token},
                     {"widthMm", decoded.boundsMm.width()},
                     {"heightMm", decoded.boundsMm.height()},
                     {"boundsMm", QVariantMap{{"x", 0.},
                                              {"y", 0.},
                                              {"width", decoded.boundsMm.width()},
                                              {"height", decoded.boundsMm.height()}}},
                     {"objectCount", decoded.items.size()},
                     {"thumbnailPng", png},
                     {"thumbnailSource", imageSource(png)}};
    lastMessage = "Eingefrorene Kontur-/PNG-Vorschau prüfen. Speichern benötigt ausdrückliche "
                  "Konvertierungsbestätigung.";
    return currentReview;
}
QString ArtLibraryController::saveSelection(QString name, QStringList tags,
                                            const QString &expectedToken, bool accepted) {
    if (mutating || signalingFailure)
        return {};
    const auto reason = blockReason();
    if (!reason.isEmpty()) {
        fail(reason);
        return {};
    }
    if (!tokenSyntax(expectedToken) || expectedToken != token || frozenGeometry.isEmpty()) {
        fail("Aktuelle Auswahlvorschau und passenden Vorschautoken verwenden.");
        return {};
    }
    if (!sourceMatches()) {
        invalidateSource();
        return {};
    }
    if (currentReview["conversionRequired"].toBool() && !accepted) {
        fail("Konvertierungsverluste zuerst ausdrücklich bestätigen.");
        return {};
    }
    Mutation mutation(*this);
    const auto id = library->add(std::move(name), std::move(tags), frozenGeometry);
    if (id.isEmpty()) {
        fail(library->message());
        return {};
    }
    frozenGeometry.clear();
    sourceItems.clear();
    sourcePage = -1;
    token.clear();
    currentReview.clear();
    lastMessage = "Geprüfte Grafik gespeichert; keine Bearbeitung aktiviert.";
    return id;
}
void ArtLibraryController::clearReview() {
    if (mutating || signalingFailure)
        return;
    frozenGeometry.clear();
    sourceItems.clear();
    sourcePage = -1;
    token.clear();
    currentReview.clear();
    lastMessage = "Grafikvorschau verworfen.";
    emit changed();
}
bool ArtLibraryController::insert(const QString &id, double xMm, double yMm) {
    if (mutating || signalingFailure)
        return false;
    const auto reason = blockReason();
    if (!reason.isEmpty())
        return fail(reason);
    Mutation mutation(*this);
    const auto bytes = library->geometry(id);
    if (bytes.isEmpty())
        return fail("Gespeicherte Grafik nicht verfügbar; kein Einfügen.");
    if (!document->insertArtGeometry(bytes, xMm, yMm))
        return fail(document->message());
    lastMessage = "Bibliotheksgrafik als nicht ausgebende Vorlage eingefügt.";
    return true;
}
bool ArtLibraryController::updateMetadata(const QString &id, QString name, QStringList tags) {
    if (mutating || signalingFailure || !library)
        return false;
    Mutation mutation(*this);
    const bool ok = library->updateMetadata(id, std::move(name), std::move(tags));
    lastMessage = library->message();
    return ok;
}
bool ArtLibraryController::remove(const QString &id) {
    if (mutating || signalingFailure || !library)
        return false;
    Mutation mutation(*this);
    const bool ok = library->remove(id);
    lastMessage = library->message();
    return ok;
}
QString ArtLibraryController::thumbnailSource(const QString &id) {
    if (!library || !library->writable())
        return {};
    if (const auto cached = thumbnails.object(id))
        return *cached;
    // Busy events may reuse already cached pictures, but never decode/render new ones.
    if (!blockReason().isEmpty())
        return {};
    if (renderingThumbnail)
        return {};
    QScopedValueRollback<bool> guard(renderingThumbnail, true);
    const auto png = library->thumbnail(id);
    if (png.isEmpty()) {
        fail(library->message());
        return {};
    }
    auto source = imageSource(png);
    thumbnails.insert(id, new QString(source));
    return source;
}
