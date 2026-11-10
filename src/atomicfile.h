#pragma once
#include <QByteArray>
#include <QString>
namespace AtomicFile {
bool write(const QString &path, const QByteArray &bytes, QString &error,
           const QString &backupSuffix = {});
}
