// SPDX-License-Identifier: GPL-3.0-only
#include "Contract.h"
#include "CurseForgeService.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>
#include <cmath>
namespace Awake::Core {
Contract::Contract()
{
    static const auto definitions = [] {
        QFile file(":/awake/core.schema.json");
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object()["$defs"].toObject() : QJsonObject{};
    }();
    m_definitions = definitions;
}
bool Contract::valid(const QString& definition, const QJsonValue& value) const
{ return m_definitions.contains(definition) && check(m_definitions[definition].toObject(), value); }
bool Contract::check(const QJsonObject& schema, const QJsonValue& value, int depth) const
{
    if (depth > 64) return false;
    const auto sub = [&](const QJsonObject& s) { return check(s, value, depth + 1); };
    if (schema.contains("$ref")) {
        const auto ref = schema["$ref"].toString();
        if (!ref.startsWith("#/$defs/") || !m_definitions.contains(ref.mid(8)) || !sub(m_definitions[ref.mid(8)].toObject())) return false;
    }
    for (const auto* kind : {"oneOf", "anyOf", "allOf"}) if (schema.contains(kind)) {
        int matches = 0;
        const auto choices = schema[kind].toArray();
        for (const auto& choice : choices) if (sub(choice.toObject())) ++matches;
        if ((QString(kind) == "oneOf" && matches != 1) || (QString(kind) == "anyOf" && matches == 0) ||
            (QString(kind) == "allOf" && matches != choices.size())) return false;
    }
    if (schema.contains("if")) {
        const auto clause = sub(schema["if"].toObject()) ? "then" : "else";
        if (schema.contains(clause) && !sub(schema[clause].toObject())) return false;
    }
    if (schema.contains("const") && schema["const"] != value) return false;
    if (schema.contains("enum") && !schema["enum"].toArray().contains(value)) return false;
    const auto type = schema["type"].toString();
    if ((!type.isEmpty()) && !((type == "object" && value.isObject()) || (type == "array" && value.isArray()) ||
        (type == "string" && value.isString()) || (type == "boolean" && value.isBool()) || (type == "null" && value.isNull()) ||
        (type == "integer" && value.isDouble() && std::isfinite(value.toDouble()) && std::floor(value.toDouble()) == value.toDouble()))) return false;
    if (value.isObject()) {
        const auto object = value.toObject(), properties = schema["properties"].toObject();
        for (const auto& required : schema["required"].toArray()) if (!object.contains(required.toString())) return false;
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (!properties.contains(it.key())) { if (schema["additionalProperties"] == false) return false; }
            else if (!check(properties[it.key()].toObject(), it.value(), depth + 1)) return false;
        }
    }
    if (value.isArray()) {
        const auto array = value.toArray();
        if ((schema.contains("minItems") && array.size() < schema["minItems"].toInt()) ||
            (schema.contains("maxItems") && array.size() > schema["maxItems"].toInt())) return false;
        if (schema.contains("items")) for (const auto& item : array) if (!check(schema["items"].toObject(), item, depth + 1)) return false;
    }
    if (value.isString()) {
        const auto string = value.toString();
        if ((schema.contains("minLength") && string.size() < schema["minLength"].toInt()) ||
            (schema.contains("maxLength") && string.size() > schema["maxLength"].toInt()) ||
            (schema.contains("pattern") && !QRegularExpression(schema["pattern"].toString()).match(string).hasMatch()) ||
            (schema["format"] == "uuid" && (QUuid(string).isNull() || QUuid(string).toString(QUuid::WithoutBraces) != string))) return false;
    }
    if (value.isDouble() && ((schema.contains("minimum") && value.toDouble() < schema["minimum"].toDouble()) ||
        (schema.contains("maximum") && value.toDouble() > schema["maximum"].toDouble()))) return false;
    return true;
}
QJsonObject Contract::capabilities(bool writes, bool extensions, bool evidence) const
{
    QJsonObject result;
    for (const auto& name : m_definitions["capabilities"].toObject()["required"].toArray()) {
        const auto method = name.toString();
        result[method] = !method.startsWith("outbox.") && (extensions || !method.startsWith("inbox.")) &&
            (writes || (method != "workspaces.create" && method != "plans.prepare" && method != "transactions.apply" && method != "recovery.run"));
    }
    if (!extensions) return result;
    for (const auto* capability : {"loaders.forge.v1", "loaders.neoforge.v1", "loaders.quilt.v1", "policies.v1", "imports.inspect", "imports.prepare"}) result[capability] = writes || QString(capability) != "imports.prepare";
    for (const auto* capability : {"providers.project", "outbox.list", "outbox.read", "outbox.configs.v1"}) result[capability] = true;
    result["providers.curseforge.v1"] = CurseForgeService().available();
    result["inbox.discard.v1"] = writes; result["inbox.discard"] = writes;
    if (evidence) { result["dependencies.resolutions.v1"] = writes; result["loaders.connector.v1"] = writes; }
    return result;
}
}
