// SPDX-License-Identifier: GPL-3.0-only
#include "ModMetadata.h"
#include <archive.h>
#include <archive_entry.h>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <toml++/toml.h>
#include <memory>
#include <optional>
#include <variant>
namespace Awake::Core {
namespace {
constexpr la_int64_t entryLimit = 1024 * 1024;
constexpr int scanLimit = 100000;
const QStringList metadataNames{"META-INF/mods.toml", "META-INF/neoforge.mods.toml", "fabric.mod.json", "quilt.mod.json", "META-INF/MANIFEST.MF"};

QHash<QString, QByteArray> readEntries(const QString& path)
{
    QHash<QString, QByteArray> found;
    std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(), archive_read_free);
    if (!reader) return found;
    archive_read_support_format_zip_seekable(reader.get());
    const auto wide = path.toStdWString();
    if (archive_read_open_filename_w(reader.get(), wide.c_str(), 64 * 1024) != ARCHIVE_OK) return found;
    archive_entry* entry = nullptr;
    for (int scanned = 0; scanned < scanLimit && found.size() < metadataNames.size(); ++scanned) {
        const int status = archive_read_next_header(reader.get(), &entry);
        if (status == ARCHIVE_EOF || status < ARCHIVE_WARN) break;
        const auto* raw = archive_entry_pathname_utf8(entry);
        const auto name = raw ? QString::fromUtf8(raw) : QString{};
        if (!metadataNames.contains(name) || found.contains(name) || archive_entry_filetype(entry) != AE_IFREG) continue;
        if (archive_entry_size_is_set(entry) && archive_entry_size(entry) > entryLimit) continue;
        QByteArray data; char buffer[16384];
        for (;;) {
            const auto read = archive_read_data(reader.get(), buffer, sizeof buffer);
            if (read < 0) { data.clear(); break; }
            if (read == 0) break;
            data.append(buffer, qsizetype(read));
            if (data.size() > entryLimit) { data.clear(); break; }
        }
        if (!data.isEmpty()) found.insert(name, data);
    }
    return found;
}

QString manifestVersion(const QByteArray& manifest)
{
    for (auto line : QString::fromUtf8(manifest).split('\n')) {
        line = line.trimmed();
        if (line.startsWith("Implementation-Version:", Qt::CaseInsensitive)) return line.section(':', 1).trimmed();
    }
    return {};
}

std::optional<toml::table> parseToml(const QByteArray& bytes)
{
    const std::string_view source(bytes.constData(), size_t(bytes.size()));
#if TOML_EXCEPTIONS
    try { return toml::parse(source); } catch (const std::exception&) { return std::nullopt; }
#else
    auto result = toml::parse(source);
    if (!result) return std::nullopt;
    return std::move(result).table();
#endif
}

QString tomlString(const toml::node_view<const toml::node>& node)
{ return node.is_string() ? QString::fromStdString(std::string(*node.value<std::string_view>())) : QString{}; }

ModMetadata forgeMetadata(const QByteArray& bytes, const QString& format, const QByteArray& manifest)
{
    ModMetadata result; const auto parsed = parseToml(bytes);
    if (!parsed) return result;
    const toml::table& table = *parsed;
    result.format = format;
    if (const auto* list = table["mods"].as_array()) for (const auto& node : *list) {
        const auto* mod = node.as_table(); if (!mod) continue;
        const toml::node_view<const toml::node> view(mod);
        const auto id = tomlString(view["modId"]); auto version = tomlString(view["version"]);
        if (version.contains("${file.jarVersion}")) version = manifestVersion(manifest);
        if (version.contains("${")) version.clear();
        if (!id.isEmpty()) result.mods.append({id, version});
    }
    if (const auto* groups = table["dependencies"].as_table()) for (const auto& [owner, value] : *groups) {
        const auto* list = value.as_array(); if (!list) continue;
        for (const auto& node : *list) {
            const auto* dep = node.as_table(); if (!dep) continue;
            const toml::node_view<const toml::node> view(dep);
            DeclaredDependency declared; declared.modId = tomlString(view["modId"]); declared.range = tomlString(view["versionRange"]).trimmed();
            const auto type = tomlString(view["type"]).toLower();
            if (!type.isEmpty()) declared.mandatory = type == "required";
            else declared.mandatory = view["mandatory"].value_or(false);
            if (type == "incompatible" || type == "discouraged" || declared.modId.isEmpty()) continue;
            result.dependencies.append(declared);
        }
    }
    result.readable = !result.mods.isEmpty();
    return result;
}

QString predicateSet(const QJsonValue& value)
{
    if (value.isString()) return value.toString().trimmed();
    if (value.isArray()) { QStringList parts; for (const auto& item : value.toArray()) if (item.isString()) parts.append(item.toString().trimmed()); return parts.join(" || "); }
    return QStringLiteral("\x01"); // unsupported structure: evaluated as Unknown
}

ModMetadata fabricMetadata(const QByteArray& bytes)
{
    ModMetadata result; QJsonParseError error; const auto object = QJsonDocument::fromJson(bytes, &error).object();
    if (error.error != QJsonParseError::NoError) return result;
    result.format = "fabric";
    const auto id = object["id"].toString(), version = object["version"].toString();
    if (!id.isEmpty()) result.mods.append({id, version.contains("${") ? QString{} : version});
    for (const auto& provided : object["provides"].toArray()) if (provided.isString()) result.mods.append({provided.toString(), version});
    const auto depends = object["depends"].toObject();
    for (auto it = depends.begin(); it != depends.end(); ++it) result.dependencies.append({it.key(), true, predicateSet(it.value())});
    result.readable = !result.mods.isEmpty();
    return result;
}

ModMetadata quiltMetadata(const QByteArray& bytes)
{
    ModMetadata result; QJsonParseError error; const auto loader = QJsonDocument::fromJson(bytes, &error).object()["quilt_loader"].toObject();
    if (error.error != QJsonParseError::NoError) return result;
    result.format = "quilt";
    const auto id = loader["id"].toString(), version = loader["version"].toString();
    if (!id.isEmpty()) result.mods.append({id, version.contains("${") ? QString{} : version});
    for (const auto& provided : loader["provides"].toArray()) {
        const auto name = provided.isString() ? provided.toString() : provided.toObject()["id"].toString();
        if (!name.isEmpty()) result.mods.append({name, version});
    }
    for (const auto& value : loader["depends"].toArray()) {
        if (value.isString()) { result.dependencies.append({value.toString(), true, {}}); continue; }
        const auto dep = value.toObject(); if (dep["id"].toString().isEmpty()) continue;
        result.dependencies.append({dep["id"].toString(), !dep["optional"].toBool(), dep.contains("versions") ? predicateSet(dep["versions"]) : QString{}});
    }
    result.readable = !result.mods.isEmpty();
    return result;
}

// --- version ordering -------------------------------------------------------------------------
using Item = std::variant<qulonglong, QString>;
QList<Item> tokens(QString value)
{
    value = value.trimmed().toLower(); QList<Item> items; QString current; int kind = 0; // 1 digits, 2 letters
    const auto flush = [&] {
        if (current.isEmpty()) return;
        if (kind == 1) { bool ok = false; const auto number = current.left(18).toULongLong(&ok); items.append(ok ? Item(number) : Item(current)); }
        else items.append(Item(current));
        current.clear();
    };
    for (const auto c : value) {
        const int next = c.isDigit() ? 1 : c.isLetter() ? 2 : 0;
        if (next == 0) { flush(); kind = 0; continue; }
        if (next != kind) flush();
        kind = next; current.append(c);
    }
    flush();
    // Trailing release markers and zeros do not change Maven ordering (1.0 == 1 == 1.0.0-final).
    static const QStringList release{"", "final", "ga", "release"};
    while (!items.isEmpty()) {
        const auto& last = items.last();
        if ((std::holds_alternative<qulonglong>(last) && std::get<qulonglong>(last) == 0) ||
            (std::holds_alternative<QString>(last) && release.contains(std::get<QString>(last)))) items.removeLast();
        else break;
    }
    return items;
}
int qualifierRank(const QString& value)
{
    static const QHash<QString, int> ranks{{"alpha", 0}, {"a", 0}, {"beta", 1}, {"b", 1}, {"milestone", 2}, {"m", 2}, {"rc", 3}, {"cr", 3}, {"pre", 3},
        {"snapshot", 4}, {"", 5}, {"final", 5}, {"ga", 5}, {"release", 5}, {"sp", 6}};
    return ranks.value(value, 7);
}
int compareItems(const std::optional<Item>& left, const std::optional<Item>& right)
{
    if (!left && !right) return 0;
    if (!left) return -compareItems(right, left);
    if (std::holds_alternative<qulonglong>(*left)) {
        const auto number = std::get<qulonglong>(*left);
        if (!right) return number == 0 ? 0 : 1;
        if (std::holds_alternative<QString>(*right)) return 1; // numbers sort after qualifiers
        const auto other = std::get<qulonglong>(*right);
        return number < other ? -1 : number > other ? 1 : 0;
    }
    const auto text = std::get<QString>(*left);
    if (!right) return qualifierRank(text) < 5 ? -1 : qualifierRank(text) == 5 ? 0 : 1;
    if (std::holds_alternative<qulonglong>(*right)) return -1;
    const auto other = std::get<QString>(*right); const auto a = qualifierRank(text), b = qualifierRank(other);
    if (a != b) return a < b ? -1 : 1;
    return a == 7 ? QString::compare(text, other) < 0 ? -1 : QString::compare(text, other) > 0 ? 1 : 0 : 0;
}

// --- Maven ranges (Forge/NeoForge versionRange) -----------------------------------------------
RangeResult mavenRange(const QString& spec, const QString& version)
{
    const auto range = spec.trimmed();
    if (range.isEmpty() || range == "*") return RangeResult::Satisfied;
    if (!range.startsWith('[') && !range.startsWith('(')) return range.contains(QRegularExpression("[\\[\\]\\(\\),]")) ? RangeResult::Unknown : RangeResult::Satisfied; // soft recommendation
    if (version.isEmpty()) return RangeResult::Unknown;
    int position = 0; bool any = false;
    while (position < range.size()) {
        while (position < range.size() && (range[position] == ',' || range[position].isSpace())) ++position;
        if (position >= range.size()) break;
        const auto open = range[position];
        if (open != '[' && open != '(') return RangeResult::Unknown;
        const auto close = range.indexOf(QRegularExpression("[\\]\\)]"), position + 1);
        if (close < 0) return RangeResult::Unknown;
        const auto body = range.mid(position + 1, close - position - 1); const auto end = range[close];
        position = close + 1;
        const auto comma = body.indexOf(',');
        if (comma < 0) {
            if (open != '[' || end != ']' || body.trimmed().isEmpty()) return RangeResult::Unknown;
            any = any || compareVersions(version, body.trimmed()) == 0; continue;
        }
        const auto lower = body.left(comma).trimmed(), upper = body.mid(comma + 1).trimmed();
        if (body.indexOf(',', comma + 1) >= 0) return RangeResult::Unknown;
        bool inside = true;
        if (!lower.isEmpty()) { const auto c = compareVersions(version, lower); inside = inside && (open == '[' ? c >= 0 : c > 0); }
        if (!upper.isEmpty()) { const auto c = compareVersions(version, upper); inside = inside && (end == ']' ? c <= 0 : c < 0); }
        any = any || inside;
    }
    return any ? RangeResult::Satisfied : RangeResult::Violated;
}

// --- SemVer predicates (Fabric/Quilt) ---------------------------------------------------------
QString stripBuild(const QString& value) { return value.section('+', 0, 0).trimmed(); }
QStringList numericParts(const QString& value) { return stripBuild(value).section('-', 0, 0).split('.'); }
std::optional<bool> predicate(const QString& raw, const QString& version)
{
    auto text = raw.trimmed();
    if (text.isEmpty() || text == "*") return true;
    QString op;
    for (const auto* candidate : {">=", "<=", ">", "<", "=", "^", "~"}) if (text.startsWith(candidate)) { op = candidate; text = text.mid(op.size()).trimmed(); break; }
    if (text.isEmpty()) return std::nullopt;
    const auto parts = numericParts(text);
    const auto wildcard = parts.indexOf(QRegularExpression("^[xX*]$"));
    const auto target = stripBuild(version);
    if (wildcard >= 0) {
        if (!op.isEmpty() && op != "=") return std::nullopt;
        const auto prefix = parts.mid(0, wildcard);
        if (prefix.isEmpty()) return true;
        auto upper = prefix; bool ok = false; const auto last = upper.last().toULongLong(&ok);
        if (!ok) return std::nullopt;
        upper.last() = QString::number(last + 1);
        return compareVersions(target, prefix.join('.')) >= 0 && compareVersions(target, upper.join('.')) < 0;
    }
    const auto c = compareVersions(target, stripBuild(text));
    if (op.isEmpty() || op == "=") return c == 0;
    if (op == ">=") return c >= 0;
    if (op == "<=") return c <= 0;
    if (op == ">") return c > 0;
    if (op == "<") return c < 0;
    bool ok = false; QStringList upper = parts;
    if (op == "^") {
        const auto major = parts.value(0).toULongLong(&ok); if (!ok) return std::nullopt;
        upper = {QString::number(major + 1)};
    } else {
        const auto major = parts.value(0).toULongLong(&ok); if (!ok) return std::nullopt;
        const auto minor = parts.size() > 1 ? parts[1].toULongLong(&ok) : 0; if (!ok) return std::nullopt;
        upper = parts.size() > 1 ? QStringList{QString::number(major), QString::number(minor + 1)} : QStringList{QString::number(major + 1)};
    }
    return c >= 0 && compareVersions(target, upper.join('.')) < 0;
}
RangeResult semverRange(const QString& spec, const QString& version)
{
    if (spec.contains(QChar(1))) return RangeResult::Unknown;
    const auto range = spec.trimmed();
    if (range.isEmpty() || range == "*") return RangeResult::Satisfied;
    if (version.isEmpty()) return RangeResult::Unknown;
    bool any = false;
    for (const auto& alternative : range.split("||")) {
        bool all = true;
        for (const auto& part : alternative.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts)) {
            const auto result = predicate(part, version);
            if (!result) return RangeResult::Unknown;
            all = all && *result;
        }
        any = any || all;
    }
    return any ? RangeResult::Satisfied : RangeResult::Violated;
}
}

int compareVersions(const QString& left, const QString& right)
{
    const auto a = tokens(left), b = tokens(right);
    for (qsizetype i = 0; i < qMax(a.size(), b.size()); ++i) {
        const auto x = i < a.size() ? std::optional<Item>(a[i]) : std::nullopt, y = i < b.size() ? std::optional<Item>(b[i]) : std::nullopt;
        if (const auto c = compareItems(x, y)) return c;
    }
    return 0;
}

RangeResult satisfies(const QString& format, const QString& range, const QString& version)
{ return format == "fabric" || format == "quilt" ? semverRange(range, version) : mavenRange(range, version); }

ModMetadata inspectJar(const QString& path, const QString& preferred)
{
    const auto entries = readEntries(path); const auto manifest = entries.value("META-INF/MANIFEST.MF");
    QList<ModMetadata> available;
    const auto add = [&](ModMetadata value) { if (value.readable) available.append(std::move(value)); };
    if (entries.contains("META-INF/mods.toml")) add(forgeMetadata(entries["META-INF/mods.toml"], "forge", manifest));
    if (entries.contains("META-INF/neoforge.mods.toml")) add(forgeMetadata(entries["META-INF/neoforge.mods.toml"], "neoforge", manifest));
    if (entries.contains("fabric.mod.json")) add(fabricMetadata(entries["fabric.mod.json"]));
    if (entries.contains("quilt.mod.json")) add(quiltMetadata(entries["quilt.mod.json"]));
    for (const auto& value : available) if (value.format == preferred) return value;
    if (preferred == "quilt") for (const auto& value : available) if (value.format == "fabric") return value;
    return available.isEmpty() ? ModMetadata{} : available.first();
}
}
