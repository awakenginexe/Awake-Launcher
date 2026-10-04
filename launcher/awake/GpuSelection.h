// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QString>
#include <QVariantMap>
class QWidget;
namespace Awake::Gpu {
bool validMode(const QString& mode);
bool needsPrompt(bool seen, int deviceCount);
QString preferenceValue(const QString& existing, const QString& mode);
QVariantMap settings();
bool confirmBeforeLaunch(QWidget* parent);
bool confirmBeforeLaunch(QWidget* parent, const QVariantMap& hardware);
bool applyBeforeJava(const QString& javaPath, QString& error);
}
