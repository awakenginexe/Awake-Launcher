// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QJsonObject>
namespace Awake::Core {
class Contract {
public:
    Contract();
    bool valid(const QString& definition, const QJsonValue& value) const;
    // extensions: manager.v0.2 features; evidence: manager.v0.2.1 additions (structured dependency review, Connector imports).
    QJsonObject capabilities(bool writes, bool extensions = true, bool evidence = false) const;
private:
    QJsonObject m_definitions;
    bool check(const QJsonObject& schema, const QJsonValue& value, int depth = 0) const;
};
}
