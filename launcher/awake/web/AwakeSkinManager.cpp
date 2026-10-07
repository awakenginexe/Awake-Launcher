// SPDX-License-Identifier: GPL-3.0-only
#include "AwakeSkinManager.h"
#include "AwakeWebAssets.h"
#include "Application.h"
#include "minecraft/auth/AccountList.h"
#include "minecraft/auth/Parsers.h"
#include "minecraft/skins/SkinModel.h"
#include "minecraft/skins/SkinRequests.h"
#include "net/NetJob.h"
#include <QApplication>
#include <QBuffer>
#include <QCryptographicHash>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>
#include <QDir>

namespace Awake::Web {
namespace {
QVariantMap error(const QString& message) { return {{"ok", false}, {"error", message}}; }
QByteArray png(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}
bool validSkin(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return false;
    QImageReader reader(&file, "PNG");
    const auto size = reader.size();
    return size.width() == 64 && (size.height() == 64 || size.height() == 32) && reader.canRead();
}
QImage capeImage(const QByteArray& bytes)
{
    if (bytes.isEmpty() || bytes.size() > 1024 * 1024) return {};
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "PNG");
    return reader.size() == QSize(64, 32) ? reader.read() : QImage{};
}
}

Skins::Skins(Assets* assets, QObject* parent, const QString& libraryDir)
    : QObject(parent), m_assets(assets), m_libraryDir(libraryDir.isEmpty() ? QDir(APPLICATION->dataRoot()).filePath("skins/awake-library") : libraryDir) {}
Skins::~Skins()
{
    if (m_reply) { disconnect(m_reply, nullptr, this, nullptr); m_reply->abort(); m_reply->deleteLater(); }
    if (m_task) { disconnect(m_task.get(), nullptr, this, nullptr); m_task->abort(); }
    if (m_assets) {
        for (const auto& entry : m_skins)
            for (const auto& key : {"textureUrl", "previewUrl"}) m_assets->removeImage(entry.view.value(key).toString());
        for (const auto& head : m_heads) m_assets->removeImage(head.second);
        for (const auto& cape : m_capes) m_assets->removeImage(cape.second);
    }
}
MinecraftAccountPtr Skins::account(const QString& id) const
{
    auto accounts = APPLICATION->accounts();
    for (int i = 0; i < accounts->count(); ++i)
        if (accounts->at(i)->internalId() == id) return accounts->at(i);
    return MinecraftAccountPtr();
}
QString Skins::headUrl(const MinecraftAccountPtr& account)
{
    if (!m_assets) return {};
    const auto data = account->accountData()->minecraftProfile.skin.data;
    const auto hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    auto& cached = m_heads[account->internalId()];
    if (cached.first == hash) return cached.second;
    m_assets->removeImage(cached.second);
    cached = {hash, {}};
    if (data.isEmpty()) {
        const QImage skin(":/awake-skins/wide/steve.png");
        auto face = skin.copy(8, 8, 8, 8);
        QPainter painter(&face);
        painter.drawImage(0, 0, skin.copy(40, 8, 8, 8));
        painter.end();
        cached.second = m_assets->putImage(png(face));
        return cached.second;
    }
    if (data.size() > 1024 * 1024) return {};
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "PNG");
    if (reader.size() != QSize(64, 64) && reader.size() != QSize(64, 32)) return {};
    const auto face = account->getFace(8, 8);
    if (!face.isNull()) cached.second = m_assets->putImage(png(face.toImage()));
    return cached.second;
}
QVariantMap Skins::addSkin(const QString& id, const QString& name, const QString& path, const QString& variant)
{
    if (path.startsWith(":/awake-skins/") && m_skins.contains(id)) return m_skins[id].view;
    if (!m_assets || !validSkin(path)) return error(tr("Skin images must be 64x64 or 64x32 pixel PNG files."));
    SkinModel skin(path);
    if (!skin.isValid()) return error(tr("Unable to decode the skin."));
    skin.setModel(variant == "SLIM" ? SkinModel::SLIM : SkinModel::CLASSIC);
    const auto texture = png(skin.getTexture());
    const auto textureHash = QString::fromLatin1(QCryptographicHash::hash(texture, QCryptographicHash::Sha256).toHex());
    const auto hash = textureHash + variant;
    if (m_skins.contains(id) && m_skins[id].hash == hash) return m_skins[id].view;
    auto& cached = m_skins[id];
    for (const auto& key : {"textureUrl", "previewUrl"}) m_assets->removeImage(cached.view.value(key).toString());
    cached = {path, hash, {{"id", id}, {"name", name}, {"variant", skin.getModelString()},
        {"textureHash", textureHash}, {"textureUrl", m_assets->putImage(texture)},
        {"previewUrl", m_assets->putImage(png(skin.getPreview().copy(0, 0, 16, 36)))}}};
    return cached.view;
}
QVariantMap Skins::importSkin(const QString& accountId, const QString& name, const QByteArray& bytes, const QString& variant)
{
    if (!m_files.isValid() || bytes.size() > 1024 * 1024) return error(tr("Skin file is too large."));
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "PNG");
    if ((reader.size() != QSize(64, 64) && reader.size() != QSize(64, 32)) || reader.read().isNull())
        return error(tr("Skin images must be 64x64 or 64x32 pixel PNG files."));
    const auto id = "preview/" + accountId;
    const auto path = m_files.filePath(QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".png");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return error(tr("Unable to save the skin preview."));
    return addSkin(id, name, path, variant);
}
QVariantMap Skins::state(const QString& accountId)
{
    const auto acct = account(accountId);
    if (!acct && !accountId.isEmpty()) return error(tr("Select a Minecraft account first."));
    QVariantList defaults;
    for (const auto& name : {"steve", "alex", "zuri", "sunny", "noor", "ari", "efe", "makena", "kai"})
        for (const auto& variant : {"CLASSIC", "SLIM"}) {
            const auto path = QString(":/awake-skins/%1/%2.png").arg(QString(variant) == "SLIM" ? "slim" : "wide", name);
            auto entry = addSkin(QString("default/%1/%2").arg(name, variant), QString(name).replace(0, 1, QString(name).left(1).toUpper()), path, variant);
            if (!entry.contains("error")) defaults.append(entry);
        }
    const auto saved = savedSkins();
    if (!acct) return {{"ok", true}, {"accountId", accountId}, {"editable", false}, {"defaults", defaults}, {"saved", saved},
        {"preview", m_skins.value("preview/" + accountId).view}, {"busy", m_busy}};
    const auto& profile = acct->accountData()->minecraftProfile;
    auto uuid = profile.id;
    uuid.remove('-');
    quint32 uuidHash = 0;
    for (int i = 0; i < 32; i += 8) uuidHash ^= uuid.mid(i, 8).toUInt(nullptr, 16);
    // Minecraft 1.21.8: UUID.hashCode(), floorMod(hash, 18), slim then wide in this order.
    const auto defaultIndex = (static_cast<qint32>(uuidHash) % 18 + 18) % 18;
    const QStringList defaultNames{"alex", "ari", "efe", "kai", "makena", "noor", "steve", "sunny", "zuri"};
    const auto defaultVariant = defaultIndex < 9 ? "SLIM" : "CLASSIC";
    const auto minecraftDefault = m_skins.value(QString("default/%1/%2").arg(defaultNames[defaultIndex % 9], defaultVariant)).view;
    QVariantMap current;
    if (!profile.skin.data.isEmpty() && profile.skin.data.size() <= 1024 * 1024) {
        const auto path = m_files.filePath(QString::fromLatin1(QCryptographicHash::hash(accountId.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".png");
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly) && file.write(profile.skin.data) == profile.skin.data.size() && file.commit())
            current = addSkin("current/" + accountId, acct->profileName(), path, profile.skin.variant.toUpper());
    }
    QVariantList capes;
    for (const auto& cape : profile.capes) {
        auto& cached = m_capes[accountId + "/" + cape.id];
        const auto hash = QCryptographicHash::hash(cape.data, QCryptographicHash::Sha256);
        if (cached.first != hash && m_assets) {
            m_assets->removeImage(cached.second);
            cached = {hash, {}};
            const auto image = capeImage(cape.data);
            if (!image.isNull()) cached.second = m_assets->putImage(png(image));
        }
        capes.append(QVariantMap{{"id", cape.id}, {"name", cape.alias}, {"textureUrl", cached.second}});
    }
    return {{"ok", true}, {"accountId", accountId}, {"editable", acct->accountType() == AccountType::MSA && !acct->accessToken().isEmpty()},
        {"current", current}, {"minecraftDefault", minecraftDefault}, {"defaults", defaults}, {"saved", saved}, {"preview", m_skins.value("preview/" + accountId).view},
        {"capes", capes}, {"capeId", profile.currentCape}, {"busy", m_busy}};
}
QVariantList Skins::savedSkins()
{
    QVariantList saved;
    const QDir dir(m_libraryDir);
    static const QRegularExpression idPattern("^[a-f0-9]{32}$");
    for (const auto& info : dir.entryInfoList({"*.json"}, QDir::Files | QDir::NoSymLinks, QDir::Time)) {
        const auto id = info.completeBaseName();
        if (!idPattern.match(id).hasMatch() || info.size() > 4096) continue;
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const auto metadata = QJsonDocument::fromJson(file.read(4097)).object();
        const auto name = metadata.value("name").toString().trimmed(), variant = metadata.value("variant").toString();
        const auto path = dir.filePath(id + ".png");
        if (name.isEmpty() || name.size() > 64 || !QStringList{"CLASSIC", "SLIM"}.contains(variant) || QFileInfo(path).isSymLink()) continue;
        auto entry = addSkin("local/" + id, name, path, variant);
        if (!entry.contains("error")) saved.append(entry);
    }
    return saved;
}
bool Skins::validSelection(const QString& id, const QString& accountId, const QString& variant) const
{
    return m_skins.contains(id) && validSkin(m_skins.value(id).path)
        && (!id.startsWith("current/") || id == "current/" + accountId)
        && (!id.startsWith("preview/") || id == "preview/" + accountId)
        && QStringList{"CLASSIC", "SLIM"}.contains(variant);
}
void Skins::download(const QUrl& url, std::function<void(QByteArray, QString)> done)
{
    static const QRegularExpression texturePath("^/texture/[a-f0-9]{32,64}$");
    const bool texture = url.host() == "textures.minecraft.net" && texturePath.match(url.path()).hasMatch();
    const bool lookup = url.host() == "api.minecraftservices.com" && url.path().startsWith("/minecraft/profile/lookup/name/");
    const bool profile = url.host() == "sessionserver.mojang.com" && url.path().startsWith("/session/minecraft/profile/");
    if (url.scheme() != "https" || !url.userInfo().isEmpty() || url.port(-1) != -1 || url.hasQuery() || url.hasFragment() || (!texture && !lookup && !profile)) {
        done({}, tr("The skin service returned an unsupported texture URL.")); return;
    }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    auto* reply = APPLICATION->network()->get(request);
    m_reply = reply;
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QIODevice::readyRead, this, [reply, bytes] {
        bytes->append(reply->readAll());
        if (bytes->size() > 1024 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, done = std::move(done)] {
        bytes->append(reply->readAll());
        const bool ok = reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 && bytes->size() <= 1024 * 1024;
        if (m_reply == reply) m_reply.clear();
        reply->deleteLater();
        done(ok ? *bytes : QByteArray{}, ok ? QString{} : tr("Unable to download the skin. Check the username and try again."));
    });
}
void Skins::complete(const QString& requestId, const QString& accountId, const QString& problem)
{
    m_busy = false;
    auto task = std::move(m_task);
    if (task) QTimer::singleShot(0, this, [task] {});
    emit changed();
    emit finished(requestId, problem.isEmpty() ? state(accountId) : error(problem));
}
void Skins::loadCapes(const QString& requestId, const QString& accountId, QStringList ids)
{
    const auto acct = account(accountId);
    if (!acct) { complete(requestId, accountId, tr("The Minecraft account is no longer available.")); return; }
    if (ids.isEmpty()) { complete(requestId, accountId); return; }
    const auto id = ids.takeFirst();
    const auto cape = acct->accountData()->minecraftProfile.capes.value(id);
    if (!capeImage(cape.data).isNull()) { loadCapes(requestId, accountId, ids); return; }
    download(QUrl(cape.url), [this, requestId, accountId, id, url = cape.url, ids](QByteArray bytes, QString problem) {
        if (!problem.isEmpty() || capeImage(bytes).isNull()) {
            complete(requestId, accountId, tr("Unable to load the cape preview. Try again.")); return;
        }
        const auto current = account(accountId);
        if (!current || !current->accountData()->minecraftProfile.capes.contains(id) || current->accountData()->minecraftProfile.capes[id].url != url) {
            complete(requestId, accountId, tr("The account's cape list changed. Try again.")); return;
        }
        current->accountData()->minecraftProfile.capes[id].data = bytes;
        loadCapes(requestId, accountId, ids);
    });
}
QVariantMap Skins::command(const QString& requestId, const QString& accountId, const QString& command, const QVariantMap& payload)
{
    static const QRegularExpression requestPattern("^[A-Za-z0-9-]{1,100}$");
    if (!requestPattern.match(requestId).hasMatch()) return error(tr("Invalid skin request."));
    const auto acct = account(accountId);
    if (!acct && (!accountId.isEmpty() || !QStringList{"browse", "lookup", "saveLocal"}.contains(command))) return error(tr("The Minecraft account is no longer available."));
    if (m_busy) return error(tr("A skin operation is already running."));
    if (!QStringList{"browse", "lookup", "capes", "apply", "reset", "saveLocal"}.contains(command)) return error(tr("Unknown skin operation."));
    if (command == "saveLocal") {
        const auto id = payload.value("id").toString(), variant = payload.value("variant").toString(), name = payload.value("name").toString().trimmed();
        if (!validSelection(id, accountId, variant)) return error(tr("Select a valid skin and model."));
        if (name.isEmpty() || name.size() > 64) return error(tr("Enter a skin name of up to 64 characters."));
        const auto bytes = png(SkinModel(m_skins.value(id).path).getTexture());
        m_busy = true;
        QTimer::singleShot(0, this, [this, requestId, accountId, name, variant, bytes] {
            const QDir dir(m_libraryDir);
            const auto id = QUuid::createUuid().toString(QUuid::Id128);
            const auto path = dir.filePath(id + ".png");
            QSaveFile image(path), metadata(dir.filePath(id + ".json"));
            const auto json = QJsonDocument(QJsonObject{{"name", name}, {"variant", variant}}).toJson(QJsonDocument::Compact);
            QString problem;
            if (!dir.mkpath(".") || bytes.isEmpty() || !image.open(QIODevice::WriteOnly) || image.write(bytes) != bytes.size() || !image.commit())
                problem = tr("Unable to save the skin to your local library.");
            else if (!metadata.open(QIODevice::WriteOnly) || metadata.write(json) != json.size() || !metadata.commit()) {
                QFile::remove(path);
                problem = tr("Unable to save the skin name to your local library.");
            }
            complete(requestId, accountId, problem);
        });
        return {{"ok", true}};
    }
    if (command == "capes") {
        m_busy = true;
        const auto ids = acct->accountData()->minecraftProfile.capes.keys();
        QTimer::singleShot(0, this, [this, requestId, accountId, ids] { loadCapes(requestId, accountId, ids); });
        return {{"ok", true}};
    }
    if (command == "apply" || command == "reset") {
        if (acct->accountType() != AccountType::MSA || acct->accessToken().isEmpty() || acct->currentTask()) return error(tr("Refresh your Microsoft account before changing its skin."));
        const auto id = payload.value("id").toString();
        const auto variant = payload.value("variant").toString();
        const auto capeId = payload.value("capeId").toString();
        if (command == "apply" && !validSelection(id, accountId, variant)) return error(tr("Select a valid skin and model."));
        if (!capeId.isEmpty() && !acct->accountData()->minecraftProfile.capes.contains(capeId)) return error(tr("The selected cape does not belong to this account."));
        // ponytail: one skin operation at a time; per-account jobs if concurrent editing is needed.
        m_busy = true;
        auto job = makeShared<NetJob>(tr("Change Minecraft skin"), APPLICATION->network(), 1);
        job->setAskRetry(false);
        if (command == "reset") job->addNetAction(makeSkinDeleteRequest(acct->accessToken()));
        else {
            job->addNetAction(makeSkinUploadRequest(acct->accessToken(), m_skins[id].path, variant));
        }
        if (capeId != acct->accountData()->minecraftProfile.currentCape) job->addNetAction(makeCapeChangeRequest(acct->accessToken(), capeId));
        job->addTask(acct->refresh().staticCast<Task>());
        m_task = job;
        connect(job.get(), &Task::succeeded, this, [this, requestId, accountId] { complete(requestId, accountId); });
        connect(job.get(), &Task::failed, this, [this, requestId, accountId](const QString&) { complete(requestId, accountId, tr("Minecraft rejected the skin change. Refresh your account and try again.")); });
        connect(job.get(), &Task::aborted, this, [this, requestId, accountId] { complete(requestId, accountId, tr("Skin change was cancelled.")); });
        job->start();
        return {{"ok", true}};
    }
    if (command == "browse") {
        m_busy = true;
        QTimer::singleShot(0, this, [this, requestId, accountId] {
            emit modalChanged(true);
            const auto path = QFileDialog::getOpenFileName(QApplication::activeWindow(), tr("Select Minecraft skin"), {}, tr("Minecraft skins (*.png)"));
            emit modalChanged(false);
            QString problem;
            if (!path.isEmpty()) {
                if (!validSkin(path)) problem = tr("Skin images must be 64x64 or 64x32 pixel PNG files.");
                else { QFile file(path); if (file.open(QIODevice::ReadOnly)) problem = importSkin(accountId, QFileInfo(path).completeBaseName(), file.read(1024 * 1024 + 1), "CLASSIC").value("error").toString(); else problem = tr("Unable to read the skin file."); }
            }
            complete(requestId, accountId, problem);
        });
        return {{"ok", true}};
    }
    const auto username = payload.value("username").toString();
    static const QRegularExpression playerName("^[A-Za-z0-9_]{1,16}$");
    if (!playerName.match(username).hasMatch()) return error(tr("Enter a valid Minecraft username."));
    m_busy = true;
    download(QUrl("https://api.minecraftservices.com/minecraft/profile/lookup/name/" + username), [this, requestId, accountId, username](QByteArray bytes, QString problem) {
        const auto id = QJsonDocument::fromJson(bytes).object().value("id").toString();
        static const QRegularExpression uuid("^[a-fA-F0-9]{32}$");
        if (!problem.isEmpty() || !uuid.match(id).hasMatch()) { complete(requestId, accountId, tr("Minecraft username was not found.")); return; }
        download(QUrl("https://sessionserver.mojang.com/session/minecraft/profile/" + id), [this, requestId, accountId, username](QByteArray profileBytes, QString profileError) {
            MinecraftProfile profile;
            if (!profileError.isEmpty() || !Parsers::parseMinecraftProfileMojang(profileBytes, profile)) { complete(requestId, accountId, tr("Unable to read that player's skin.")); return; }
            download(QUrl(profile.skin.url), [this, requestId, accountId, username, variant = profile.skin.variant.toUpper()](QByteArray skinBytes, QString skinError) {
                if (skinError.isEmpty()) skinError = importSkin(accountId, username, skinBytes, variant).value("error").toString();
                complete(requestId, accountId, skinError);
            });
        });
    });
    return {{"ok", true}};
}
}
