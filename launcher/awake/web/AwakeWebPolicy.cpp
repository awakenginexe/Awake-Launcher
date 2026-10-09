// SPDX-License-Identifier: GPL-3.0-only
#include "AwakeWebPolicy.h"
#include <QRegularExpression>
#include <QSet>
namespace Awake::Web {
namespace {
bool originAllowed(const QUrl& url)
{
    return url.isValid() && url.scheme() == "awake" && url.host() == "ui" && url.userInfo().isEmpty() &&
           url.port(-1) == -1 && !url.hasQuery() && !url.hasFragment();
}
}

QString resourcePath(const QUrl& url)
{
    if (!originAllowed(url)) return {};
    auto path = url.path(QUrl::FullyEncoded);
    // No decoding/normalization: percent-escaped separators and repeated encoding are outside the resource namespace.
    if (path.isEmpty() || path == "/") path = "/index.html";
    if (path == "/assets/minecraft-background.png") return ":/backgrounds/awake-minecraft";
    if (path == "/index.html" || path == "/qwebchannel.js") return ":/awake-web" + path;
    static const QRegularExpression asset("^/assets/[A-Za-z0-9_-]+(?:\\.[A-Za-z0-9_-]+)+$");
    if (asset.match(path).hasMatch()) return ":/awake-web" + path;
    return {};
}

bool internalUrl(const QUrl& url)
{
    if (!resourcePath(url).isEmpty()) return true;
    static const QRegularExpression image("^/images/[a-f0-9]{16,32}\\.png$");
    return originAllowed(url) && image.match(url.path(QUrl::FullyEncoded)).hasMatch();
}

bool externalUrl(const QUrl& url)
{
    return url.isValid() && (url.scheme() == "https" || url.scheme() == "http") && !url.host().isEmpty() &&
           url.userInfo().isEmpty();
}

bool actionAllowed(const QString& action)
{
    static const QSet<QString> actions{
        "create", "import", "edit", "folder", "accounts", "settings", "manage", "launch",
        "rename", "changeGroup", "copy", "export", "delete", "kill", "installPack", "importArchive",
        "launchOptions", "application", "logs", "legacy",
        "addMicrosoft", "addOffline", "removeAccount", "setDefaultAccount", "refreshAccount", "createQuick",
        "windowMinimize", "windowMaximize", "windowClose",
        "openRootFolder", "openInstancesFolder", "openModsFolder", "openLogsFolder", "openJavaFolder", "openSkinsFolder",
        "checkForUpdates", "clearMetadata", "reportBug", "about", "discord", "reddit", "matrix"
    };
    return actions.contains(action);
}

bool preferenceAllowed(const QString& key, const QVariant& value)
{
    if (key == "jvmArgs") return value.metaType().id() == QMetaType::QString && value.toString().size() <= 8192 && !value.toString().contains(QChar(0));
    if (key == "jvmPreset") return value.metaType().id() == QMetaType::QString &&
        QStringList{"compatible", "balanced", "performance", "custom"}.contains(value.toString());
    if (key == "javaProfile") return value.metaType().id() == QMetaType::QString &&
        QStringList{"awake", "minecraft", "microsoft", "graalvm", "temurin", "zulu", "oracle", "custom"}.contains(value.toString());
    if (key == "reducedMotion" || key == "compact" || key == "maximizeGame" || key == "closeOnLaunch") {
        return value.metaType().id() == QMetaType::Bool;
    }
    if (key == "sortMode") {
        return value.metaType().id() == QMetaType::QString && QStringList{"Name", "LastLaunch", "TotalTimePlayed"}.contains(value.toString());
    }
    if (key == "language") {
        return value.metaType().id() == QMetaType::QString &&
               QStringList{"en", "th", "zh-CN", "zh-TW"}.contains(value.toString());
    }
    if (key == "minMem" || key == "maxMem" || key == "gameWidth" || key == "gameHeight") {
        return value.canConvert<int>() && value.toInt() > 0;
    }
    if (key == "pin" && value.metaType().id() == QMetaType::QVariantMap) {
        const auto map = value.toMap();
        return map.size() == 2 && map.value("id").metaType().id() == QMetaType::QString && !map.value("id").toString().isEmpty() &&
               map.value("pinned").metaType().id() == QMetaType::Bool;
    }
    return false;
}

QString frontendLocale(QString locale)
{
    locale.replace('_', '-');
    if (locale.startsWith("th", Qt::CaseInsensitive)) return "th";
    if (locale.compare("zh-TW", Qt::CaseInsensitive) == 0 || locale.startsWith("zh-Hant", Qt::CaseInsensitive) || locale.compare("zh-HK", Qt::CaseInsensitive) == 0) return "zh-TW";
    if (locale.startsWith("zh", Qt::CaseInsensitive)) return "zh-CN";
    return "en";
}

QString nativeLocale(QString locale)
{
    if (locale == "en") return "en_US";
    if (locale == "th") return "th";
    if (locale == "zh-CN") return "zh_CN";
    if (locale == "zh-TW") return "zh_TW";
    return {};
}

QVariantMap instanceDto(const InstanceData& i)
{
    return {{"id", i.id}, {"name", i.name}, {"group", i.group}, {"minecraftVersion", i.minecraftVersion},
            {"loader", i.loader}, {"loaderVersion", i.loaderVersion}, {"iconUrl", i.iconUrl}, {"pinned", i.pinned},
            {"canLaunch", i.canLaunch}, {"running", i.running}, {"broken", i.broken},
            {"lastLaunch", i.lastLaunch}, {"lastTimePlayed", i.lastTimePlayed >= 0 ? QVariant::fromValue(i.lastTimePlayed) : QVariant{}},
            {"totalTimePlayed", i.totalTimePlayed}};
}
}
