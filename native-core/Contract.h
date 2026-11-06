// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QJsonObject>
namespace Awake::Core {
class Contract {
public:
    Contract();
    bool valid(const QString& definition, const QJsonValue& value) const;
    QJsonObject capabilities(bool writes) const;
private:
    QJsonObject m_definitions;
    bool check(const QJsonObject& schema, const QJsonValue& value, int depth = 0) const;
};
}
