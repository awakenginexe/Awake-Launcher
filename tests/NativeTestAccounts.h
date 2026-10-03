// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QFile>

inline void seedStartupAccount(const QString& directory)
{
    // Prevent the first-run login wizard from blocking Application's constructor in native tests.
    QFile file(directory + "/accounts.json");
    if (!file.open(QIODevice::WriteOnly) || file.write(R"({"formatVersion":3,"accounts":[{"type":"MSA","entitlement":{"ownsMinecraft":true,"canPlayMinecraft":true}}]})") < 0)
        qFatal("Cannot write native startup account fixture");
}
