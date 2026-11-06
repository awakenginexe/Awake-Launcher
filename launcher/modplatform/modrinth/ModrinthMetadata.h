// SPDX-FileCopyrightText: 2022-2023 flowln <flowlnlnln@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QString>
#include <QUrl>
#include "modplatform/ResourceType.h"
namespace Modrinth {
QString authorUrl(const QString& name);
QString minecraftVersion(QString version);
ModPlatform::ResourceType resourceType(const QString& type);
QString resourceParameter(ModPlatform::ResourceType type);
QUrl searchUrl(const QString& base, const QString& query, const QString& facets, int offset, int limit, const QString& order = "relevance");
QUrl versionsUrl(const QString& base, const QString& project, const QStringList& games, const QStringList& loaders, bool changelog);
}
