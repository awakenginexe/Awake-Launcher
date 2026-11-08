// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QByteArray>
#include <QCryptographicHash>
#include <QString>
#include <functional>
namespace Awake::Integration::DurableIO {
struct FileState { QString hash, identity; };
FileState fingerprint(const QString& path, QString* error = nullptr, const QString& accessReference = {});
bool stage(const QString& source, const QString& incoming, const QString& expectedHash,
           const std::function<bool(const QString&)>& recordIdentity, QString* error);
bool exchange(const QString& source, const FileState& sourceState, const QString& destination,
              const FileState& expected, const QString& displaced, QString* error,
              const std::function<bool()>& afterDisplace = {});
bool discardIncoming(const QString& incoming, const QString& identity, const QString& staged, QString* error);
bool restoreAccess(const QString& path, const FileState& expected, QString* error);
bool write(const QString& path, const QByteArray& bytes, QString* error = nullptr);
bool copy(const QString& source, const QString& destination, QString* error = nullptr);
QString hash(const QString& path, QString* error = nullptr, QCryptographicHash::Algorithm algorithm = QCryptographicHash::Sha256);
// Reuse hashes of files whose NTFS identity, size and write time are unchanged. Off by default.
void setHashCacheEnabled(bool enabled);
bool remove(const QString& path, QString* error = nullptr);
}  // namespace Awake::Integration::DurableIO
