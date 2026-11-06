// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QByteArray>
#include <QString>
namespace Awake::Integration::DurableIO {
bool write(const QString& path, const QByteArray& bytes, QString* error = nullptr);
bool copy(const QString& source, const QString& destination, QString* error = nullptr);
QString hash(const QString& path, QString* error = nullptr);
bool remove(const QString& path, QString* error = nullptr);
}  // namespace Awake::Integration::DurableIO
