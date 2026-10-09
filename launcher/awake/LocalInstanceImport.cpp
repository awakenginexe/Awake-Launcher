// SPDX-License-Identifier: GPL-3.0-only
#include "LocalInstanceImport.h"
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QSaveFile>
#include "archive/ArchiveReader.h"
#include "Json.h"
#include "modplatform/atlauncher/ATLPackIndex.h"
#include "settings/INIFile.h"

namespace Awake::LocalImport {
namespace {
QString message(const char* text) { return QObject::tr(text); }
QString numericId(const QJsonValue& value)
{
    const auto text = value.isString() ? value.toString() : QString::number(value.toVariant().toLongLong());
    static const QRegularExpression pattern("^[1-9][0-9]{0,15}$");
    return pattern.match(text).hasMatch() ? text : QString();
}
bool hasMetadata(const QString& path)
{
    for (const auto& file : {"instance.cfg", "minecraftinstance.json", "instance.json"})
        if (QFileInfo::exists(path + '/' + file)) return true;
    return false;
}
QJsonObject readJson(const QString& path, QString* error)
{
    QFile file(path);
    if (QFileInfo(path).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) {
        *error = message("Cannot read instance metadata: %1").arg(path);
        return {};
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        *error = message("Invalid instance metadata: %1").arg(path);
    return document.object();
}
void setLoader(Instance& item, QString type, QString version)
{
    type = type.toLower();
    if (type == "forge") item.loader = "net.minecraftforge";
    else if (type == "neoforge") item.loader = "net.neoforged";
    else if (type == "fabric") item.loader = "net.fabricmc.fabric-loader";
    else if (type == "quilt") item.loader = "org.quiltmc.quilt-loader";
    else { item.error = message("Unsupported mod loader: %1").arg(type); return; }
    if (version.startsWith(item.minecraft + '-')) version.remove(0, item.minecraft.size() + 1);
    item.loaderVersion = version;
    if (version.isEmpty()) item.error = message("The mod loader version is missing.");
}
bool nested(const QString& parent, const QString& child)
{
#ifdef Q_OS_WIN
    const auto sensitivity = Qt::CaseInsensitive;
#else
    const auto sensitivity = Qt::CaseSensitive;
#endif
    return parent.compare(child, sensitivity) == 0 || child.startsWith(parent + '/', sensitivity);
}
}

Instance inspect(const QString& path)
{
    Instance item{.path = QFileInfo(path).canonicalFilePath(), .name = QFileInfo(path).fileName()};
    if (item.path.isEmpty() || !QFileInfo(path).isDir() || QFileInfo(path).isSymLink() || QFileInfo(path).isJunction()) {
        item.error = message("Choose an existing instance folder, not a shortcut or linked folder.");
        return item;
    }
    if (QFileInfo::exists(item.path + "/instance.cfg")) {
        item.source = "Prism / MultiMC";
        INIFile config;
        if (!config.loadFile(item.path + "/instance.cfg") || config.get("InstanceType", "").toString() != "OneSix") {
            item.error = message("This Prism/MultiMC instance format is not supported.");
            return item;
        }
        item.name = config.get("name", item.name).toString();
        const auto root = readJson(item.path + "/mmc-pack.json", &item.error);
        for (const auto& value : root.value("components").toArray()) {
            const auto component = value.toObject();
            const auto uid = component.value("uid").toString();
            if (uid == "net.minecraft") item.minecraft = component.value("version").toString();
            if (uid == "net.minecraftforge" || uid == "net.neoforged" || uid == "net.fabricmc.fabric-loader" || uid == "org.quiltmc.quilt-loader") {
                item.loader = uid;
                item.loaderVersion = component.value("version").toString();
            }
        }
        if (!QDir(item.path + "/minecraft").exists() && !QDir(item.path + "/.minecraft").exists())
            item.error = message("The instance has no minecraft or .minecraft game folder.");
    } else if (QFileInfo::exists(item.path + "/minecraftinstance.json")) {
        item.source = "CurseForge";
        const auto root = readJson(item.path + "/minecraftinstance.json", &item.error);
        item.name = root.value("name").toString(item.name);
        item.minecraft = root.value("gameVersion").toString();
        const auto loader = root.value("baseModLoader").toObject();
        if (!loader.isEmpty()) {
            const auto parts = loader.value("name").toString().split('-');
            const auto type = parts.value(0).toLower();
            auto version = loader.value("forgeVersion").toString();
            if (version.isEmpty()) version = parts.value(1);
            setLoader(item, type, version);
        }
        if (root.value("isMemoryOverride").toBool()) item.memory = root.value("allocatedMemory").toInt();
        const auto pack = root.value("installedModpack").toObject();
        item.pack = {"curseforge", numericId(pack.value("addonID")), pack.value("name").toString(item.name),
            numericId(pack.value("fileID")), pack.value("version").toString()};
        if (item.pack.id.isEmpty()) item.pack.id = numericId(pack.value("addonId"));
        if (item.pack.versionId.isEmpty()) item.pack.versionId = numericId(pack.value("installedFile").toObject().value("id"));
        if (item.pack.versionId.isEmpty()) item.pack.versionId = numericId(pack.value("fileId"));
    } else if (QFileInfo::exists(item.path + "/instance.json")) {
        const auto root = readJson(item.path + "/instance.json", &item.error);
        if (root.value("launcher").isObject()) {
            item.source = "ATLauncher";
            const auto launcher = root.value("launcher").toObject();
            item.name = launcher.value("name").toString(item.name);
            item.minecraft = root.value("id").toString();
            const auto loader = launcher.value("loaderVersion").toObject();
            if (!loader.isEmpty()) setLoader(item, loader.value("type").toString(), loader.value("version").toString());
            item.memory = launcher.value("maximumMemory").toInt();
            if (item.memory < 512 || item.memory > 1048576) item.memory = launcher.value("requiredMemory").toInt();
            const auto cf = launcher.value("curseForgeProject").toObject();
            const auto cfFile = launcher.value("curseForgeFile").toObject();
            const auto mr = launcher.value("modrinthProject").toObject();
            const auto mrVersion = launcher.value("modrinthVersion").toObject();
            if (!cf.isEmpty()) item.pack = {"curseforge", numericId(cf.value("id")), cf.value("name").toString(), numericId(cfFile.value("id")), cfFile.value("displayName").toString()};
            else if (!mr.isEmpty()) item.pack = {"modrinth", mr.value("id").toString(), mr.value("title").toString(), mrVersion.value("id").toString(), mrVersion.value("version_number").toString()};
            else if (launcher.value("packId").toInt() > 0 && !launcher.value("pack").toString().isEmpty()) {
                const auto safeName = ATLauncher::packId(launcher.value("pack").toString());
                item.pack = {"atlauncher", safeName, launcher.value("pack").toString(), launcher.value("version").toString(), launcher.value("version").toString()};
            }
        } else if (root.contains("mcVersion")) {
            item.source = "FTB App";
            item.name = root.value("name").toString(item.name);
            item.minecraft = root.value("mcVersion").toString();
            const auto loader = root.value("modLoader").toString();
            if (!loader.isEmpty()) setLoader(item, loader.section('-', 0, 0), loader.section('-', 1));
            item.playtime = root.value("totalPlayTime").toVariant().toLongLong() / 1000;
        } else if (item.error.isEmpty()) item.error = message("The instance.json format is not supported.");
    } else {
        item.error = message("Select an instance containing minecraftinstance.json, instance.json, or instance.cfg and mmc-pack.json. Do not select mods or saves.");
    }
    static const QRegularExpression versionPattern("^[A-Za-z0-9][A-Za-z0-9._+\\-]{0,127}$");
    if (item.error.isEmpty() && (!versionPattern.match(item.minecraft).hasMatch() ||
        (!item.loader.isEmpty() && !versionPattern.match(item.loaderVersion).hasMatch())))
        item.error = message("The Minecraft or mod loader version is missing or invalid.");
    if (item.name.trimmed().isEmpty()) item.name = QFileInfo(item.path).fileName();
    static const QRegularExpression packIdPattern("^[A-Za-z0-9][A-Za-z0-9._+\\-]{0,127}$");
    if (!packIdPattern.match(item.pack.id).hasMatch() || !packIdPattern.match(item.pack.versionId).hasMatch()) item.pack = {};
    return item;
}

bool writePackBaseline(const QString& archive, const QString& destination, const QString& provider,
                       const QString& minecraft, const QString& loader, const QString& loaderVersion, QString* error)
{
    const bool cf = provider == "curseforge";
    if (!cf && provider != "modrinth") { *error = message("Unsupported pack provider."); return false; }
    const auto manifestName = cf ? QString("manifest.json") : QString("modrinth.index.json");
    QByteArray manifest;
    QStringList paths;
    MMCZip::ArchiveReader reader(archive);
    bool invalid = false;
    const auto safePath = [](const QString& path) {
        return !path.isEmpty() && !path.contains('\\') && !path.contains(':') && !path.contains('\n') && !path.contains('\r') &&
            !path.startsWith('/') && !path.split('/').contains("..") && !path.contains(QChar::Null);
    };
    const bool parsed = reader.parse([&](MMCZip::ArchiveReader::File* file) {
        const auto path = file->filename();
        if (!safePath(path) || paths.size() >= 100000) { invalid = true; return false; }
        if (file->isFile()) paths.append(path);
        if (path == manifestName) {
            if (!manifest.isEmpty() || !file->isFile()) { invalid = true; return false; }
            int status = 0;
            manifest = file->readAll(&status, 16 * 1024 * 1024);
            if (status < 0 || manifest.isEmpty()) { invalid = true; return false; }
        }
        return true;
    });
    QJsonParseError parseError;
    const auto root = QJsonDocument::fromJson(manifest, &parseError).object();
    if (!parsed || invalid || parseError.error != QJsonParseError::NoError || root.isEmpty()) {
        *error = message("The selected release contains invalid pack metadata or paths."); return false;
    }
    Instance expected;
    if (cf) {
        if (root.value("manifestType").toString() != "minecraftModpack" || root.value("manifestVersion").toInt() != 1 || !root.value("files").isArray()) {
            *error = message("The selected release is not a supported CurseForge modpack."); return false;
        }
        expected.minecraft = root.value("minecraft").toObject().value("version").toString();
        for (const auto& value : root.value("minecraft").toObject().value("modLoaders").toArray()) {
            const auto id = value.toObject().value("id").toString();
            if (!value.isObject() || !value.toObject().value("id").isString()) invalid = true;
            if (value.toObject().value("primary").toBool()) setLoader(expected, id.section('-', 0, 0), id.section('-', 1));
        }
        for (const auto& value : root.value("files").toArray()) {
            const auto file = value.toObject();
            const auto project = Json::requireInteger(file, "projectID");
            const auto release = Json::requireInteger(file, "fileID");
            if (!value.isObject() || !project || !release || *project <= 0 || *release <= 0) invalid = true;
        }
    } else {
        if (root.value("game").toString() != "minecraft" || root.value("formatVersion").toInt() != 1 || !root.value("files").isArray()) invalid = true;
        const auto dependencies = root.value("dependencies").toObject();
        for (auto it = dependencies.begin(); it != dependencies.end(); ++it)
            if (!QStringList{"minecraft", "forge", "neoforge", "fabric-loader", "quilt-loader"}.contains(it.key()) || !it->isString()) invalid = true;
        expected.minecraft = dependencies.value("minecraft").toString();
        for (const auto& type : {"forge", "neoforge", "fabric-loader", "quilt-loader"}) {
            if (dependencies.contains(type)) setLoader(expected, QString(type).section('-', 0, 0), dependencies.value(type).toString());
        }
        static const QRegularExpression sha512("^[A-Fa-f0-9]{128}$");
        for (const auto& value : root.value("files").toArray()) {
            const auto file = value.toObject();
            if (!value.isObject() || !safePath(file.value("path").toString()) ||
                !sha512.match(file.value("hashes").toObject().value("sha512").toString()).hasMatch()) invalid = true;
        }
    }
    if (invalid || !expected.error.isEmpty() || expected.minecraft != minecraft || expected.loader != loader || expected.loaderVersion != loaderVersion) {
        *error = message("Select the pack release matching the installed Minecraft and mod loader versions."); return false;
    }
    if (QFileInfo(destination).exists() && (!QFileInfo(destination).isDir() || !QDir(destination).isEmpty())) {
        *error = message("Pack metadata destination is not empty."); return false;
    }
    if (!QDir().mkpath(destination)) { *error = message("Cannot create pack metadata folder."); return false; }
    const auto write = [&](const QString& name, const QByteArray& bytes) {
        QSaveFile file(destination + '/' + name);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
    };
    if (!write(manifestName, manifest)) { *error = message("Cannot save pack metadata."); return false; }
    const auto overrides = cf ? root.value("overrides").toString("overrides") : QString("overrides");
    if (!overrides.isEmpty() && !safePath(overrides)) { *error = message("Invalid overrides folder."); return false; }
    for (const auto& prefix : cf ? QStringList{overrides} : QStringList{"overrides", "client-overrides"}) {
        QStringList entries;
        if (!prefix.isEmpty()) for (const auto& path : paths) if (path.startsWith(prefix + '/')) entries.append(path.mid(prefix.size() + 1));
        if (!write(prefix == "client-overrides" ? "client-overrides.txt" : "overrides.txt", entries.isEmpty() ? QByteArray() : (entries.join('\n') + '\n').toUtf8())) {
            *error = message("Cannot save pack override metadata."); return false;
        }
    }
    return true;
}

QList<Instance> scan(const QString& path)
{
    if (hasMetadata(path)) return {inspect(path)};
    auto root = path;
    if (QDir(path + "/Instances").exists()) root += "/Instances";
    else if (QDir(path + "/instances").exists()) root += "/instances";
    QList<Instance> instances;
    for (const auto& folder : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (hasMetadata(folder.absoluteFilePath())) instances.append(inspect(folder.absoluteFilePath()));
    }
    return instances;
}

QStringList defaultLocations(const QString& source)
{
    const auto home = QDir::homePath();
    const auto documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const auto roaming = qEnvironmentVariable("APPDATA");
    QStringList paths;
    if (source == "auto" || source == "curseforge")
        paths << home + "/curseforge/minecraft" << documents + "/curseforge/minecraft" << documents + "/Curse/Minecraft";
    if (source == "auto" || source == "atlauncher")
        paths << roaming + "/ATLauncher" << home + "/.local/share/ATLauncher" << home + "/Library/Application Support/ATLauncher";
    if (source == "auto" || source == "prism")
        paths << roaming + "/PrismLauncher" << home + "/.local/share/PrismLauncher" << home + "/Library/Application Support/PrismLauncher";
    if (source == "auto" || source == "multimc")
        paths << roaming + "/MultiMC" << home + "/.local/share/multimc" << home + "/Library/Application Support/MultiMC";
    if (source == "auto" || source == "ftb")
        paths << home + "/.ftba/instances" << roaming + "/.ftba/instances";
    paths.removeDuplicates();
    return paths;
}

bool copyInstance(const Instance& instance, const QString& destination, QString* error)
{
    const auto fresh = inspect(instance.path);
    auto fail = [error](const QString& detail) { *error = detail; return false; };
    if (!fresh.error.isEmpty()) return fail(fresh.error);
    if (fresh.minecraft != instance.minecraft || fresh.loader != instance.loader || fresh.loaderVersion != instance.loaderVersion || fresh.pack != instance.pack)
        return fail(message("The source instance changed. Scan the folder again before importing."));
    const auto target = QDir::cleanPath(QFileInfo(destination).absoluteFilePath());
    auto ancestor = QFileInfo(target);
    while (!ancestor.exists() && ancestor.absoluteFilePath() != ancestor.absolutePath()) ancestor = QFileInfo(ancestor.absolutePath());
    if (nested(instance.path, target) || nested(instance.path, ancestor.canonicalFilePath()) ||
        (QFileInfo(target).exists() && (!QFileInfo(target).isDir() || !QDir(target).isEmpty())))
        return fail(message("The import destination must be an empty, separate instance folder."));
    const bool prism = instance.source == "Prism / MultiMC";
    const auto output = prism ? target : target + "/minecraft";
    if (!QDir().mkpath(output)) return fail(message("Cannot create the import destination."));
    QSet<QString> excluded = {"accounts.json", "launcher_accounts.json", "launcher_profiles.json", "awakelauncher.cfg",
        "prismlauncher.cfg", "multimc.cfg", "minecraftinstance.json", "instance.json", ".ftbapp"};
    if (!prism) excluded.unite({"libraries", "assets", "bin", "natives", "runtimes"});
    QDirIterator files(instance.path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    const QDir root(instance.path);
    while (files.hasNext()) {
        files.next();
        const auto info = files.fileInfo();
        const auto relative = root.relativeFilePath(info.absoluteFilePath());
        if (excluded.contains(relative.section('/', 0, 0))) continue;
        if (info.isSymLink() || info.isJunction() || !nested(instance.path, info.canonicalFilePath()))
            return fail(message("Linked files or folders cannot be imported: %1").arg(relative));
        auto importedPath = relative;
        if (instance.source == "ATLauncher" && relative.section('/', 0, 0) == "disabledmods") {
            if (info.isDir()) continue;
            importedPath = "mods/" + relative.mid(QString("disabledmods/").size());
            if (!importedPath.endsWith(".disabled")) importedPath += ".disabled";
        }
        const auto dest = output + '/' + importedPath;
        if (info.isDir()) {
            if (!QDir().mkpath(dest)) return fail(message("Cannot create folder: %1").arg(relative));
        } else if (!info.isFile() || !QDir().mkpath(QFileInfo(dest).absolutePath()) || !QFile::copy(info.absoluteFilePath(), dest)) {
            return fail(message("Cannot copy file: %1").arg(relative));
        }
    }
    return true;
}
}
