#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <QVariantMap>
namespace NativeDesign {
enum class ImportIntent { PreserveProjectSemantics, GeometryOnly };
struct ReadOptions {
    ImportIntent intent = ImportIntent::PreserveProjectSemantics;
    bool allowSystemFontSubstitution = false;
};
struct CompatibilityIssue {
    QString code, severity, scope, field, label;
    int sourceIndex = -1, count = 1;
};
struct CompatibilityReport {
    QString format;
    QByteArray sourceSha256;
    QList<CompatibilityIssue> issues;
    bool complete = true, geometryOnlyRequired = true, blocked = false, canImportGeometry = true;
    bool geometryValidated = false;
    QVariantMap toVariantMap() const;
};
// Bounded syntax/semantic-loss preflight. Never loads a resource or copies source
// controller parameters. A positive result certifies no migration fidelity.
CompatibilityReport compatibility(const QByteArray &bytes, bool millmage, ReadOptions options = {});
} // namespace NativeDesign
