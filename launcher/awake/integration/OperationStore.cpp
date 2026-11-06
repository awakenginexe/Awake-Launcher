// SPDX-License-Identifier: GPL-3.0-only
#include "OperationStore.h"
#include "DurableIO.h"
#include "ProfileAccess.h"
#include "Protocol.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
namespace Awake::Integration {
namespace {
QByteArray canonical(const QJsonObject& value) { return QJsonDocument(value).toJson(QJsonDocument::Compact); }
QString digest(const QByteArray& value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}
bool safeKey(const QString& key)
{
    static const QRegularExpression pattern(QRegularExpression::anchoredPattern("[A-Za-z0-9_-]{1,128}"));
    return pattern.match(key).hasMatch();
}
bool validId(const QString& id)
{
    return !QUuid(id).isNull() && QUuid(id).toString(QUuid::WithoutBraces) == id;
}
bool validState(const QString& state)
{
    static const QSet<QString> states{"queued", "resolving", "downloading", "prepared", "acquiring", "revalidating", "backing_up",
        "applying", "verifying", "completed", "cancelled", "failed", "recovery_required", "rolling_back", "rolled_back"};
    return states.contains(state);
}
bool allowed(const QString& before, const QString& after, const QString& method)
{
    if (OperationStore::terminal(before) || !validState(after)) return false;
    if (after == "recovery_required") return true;
    if (before == "recovery_required" && after == "verifying") return true;
    if (after == "failed") return before != "applying" && before != "verifying" && before != "rolling_back" && before != "recovery_required";
    if (after == "rolling_back") return before == "applying" || before == "verifying" || before == "recovery_required";
    if (after == "rolled_back") return before == "rolling_back";
    if (after == "cancelled") return before == "queued" || before == "resolving" || before == "downloading" || before == "prepared";
    if (before == "resolving" && (after == "prepared" || (after == "verifying" && method == "workspaces.create"))) return true;
    static const QStringList order{"queued", "resolving", "downloading", "prepared", "acquiring", "revalidating", "backing_up", "applying", "verifying", "completed"};
    const auto index = order.indexOf(before);
    return index >= 0 && index + 1 < order.size() && order[index + 1] == after;
}
}  // namespace
OperationStore::OperationStore(QString directory) : m_directory(std::move(directory)) {}
OperationStore::~OperationStore() = default;
bool OperationStore::open(QString* error)
{
    QMutexLocker lock(&m_mutex);
    const auto reject = [error](const QString& message) { if (error) *error = message; return false; };
    if (m_owner) return reject("Operation store is already open.");
    const QFileInfo requested(m_directory);
    if (requested.isSymLink() || requested.isJunction() || !createPrivateStorageDirectory(m_directory))
        return reject("Operation storage is not a private local directory.");
    m_directory = QFileInfo(m_directory).canonicalFilePath();
    auto owner = std::make_unique<QLockFile>(QDir(m_directory).filePath("store.lock"));
    owner->setStaleLockTime(0);
    if (!owner->tryLock()) return reject("Another process owns operation storage.");
    QMap<QString, QJsonObject> records;
    QSet<QString> keys;
    quint64 sequence = 0;
    for (const auto& entry : QDir(m_directory).entryInfoList({"*.json"}, QDir::Files | QDir::Hidden | QDir::System)) {
        QString ioError;
        if (!validId(entry.completeBaseName()) || !storageAccessIsPrivate(entry.absoluteFilePath()) ||
            DurableIO::hash(entry.absoluteFilePath(), &ioError).isEmpty() || entry.size() > 8 * 1024 * 1024)
            return reject("Invalid operation record: " + entry.fileName());
        QFile file(entry.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) return reject("Cannot read operation record.");
        const auto bytes = file.read(8 * 1024 * 1024 + 1);
        if (file.error() != QFileDevice::NoError || bytes.size() > 8 * 1024 * 1024) return reject("Operation record read failed.");
        const auto envelope = QJsonDocument::fromJson(bytes).object();
        const auto record = envelope["record"].toObject();
        bool sequenceOk = false;
        const auto recordSequence = record["sequence"].toString().toULongLong(&sequenceOk);
        if (envelope.size() != 2 || record["format"].toInt() != 1 || record["operationId"] != entry.completeBaseName() ||
            !validState(record["state"].toString()) || !sequenceOk || !recordSequence ||
            envelope["sha256"].toString() != digest(canonical(record)) || record["requests"].toArray().isEmpty())
            return reject("Corrupt operation record: " + entry.fileName());
        for (const auto& value : record["requests"].toArray()) {
            const auto request = value.toObject();
            const auto key = request["idempotencyKey"].toString();
            if (!safeKey(key) || keys.contains(key) || request["requestHash"].toString().size() != 64 || request["method"].toString().isEmpty())
                return reject("Corrupt idempotency record.");
            keys.insert(key);
        }
        records.insert(entry.completeBaseName(), record);
        sequence = qMax(sequence, recordSequence);
    }
    m_records = std::move(records);
    m_sequence = sequence;
    m_owner = std::move(owner);
    return true;
}
bool OperationStore::save(QJsonObject& record, QString* error)
{
    record["updatedAt"] = QString::number(QDateTime::currentMSecsSinceEpoch());
    record["sequence"] = QString::number(m_sequence + 1);
    const auto bytes = canonical({{"record", record}, {"sha256", digest(canonical(record))}});
    if (bytes.size() > 8 * 1024 * 1024) { if (error) *error = "Operation record exceeds 8 MiB."; return false; }
    if (!DurableIO::write(QDir(m_directory).filePath(record["operationId"].toString() + ".json"), bytes, error)) return false;
    ++m_sequence;
    m_records[record["operationId"].toString()] = record;
    return true;
}
QJsonObject OperationStore::status(const QJsonObject& record) const
{
    QJsonObject result;
    for (const auto* key : {"operationId", "method", "state", "result", "error", "createdAt", "updatedAt", "sequence", "progress", "cancellationRequested"})
        if (record.contains(key)) result[key] = record[key];
    result["terminal"] = terminal(record["state"].toString());
    return result;
}
QJsonObject OperationStore::replay(const QString& method, const QString& key, const QJsonObject& request, const QString& operationId) const
{
    QMutexLocker lock(&m_mutex);
    return replayLocked(method, key, request, operationId);
}
QJsonObject OperationStore::replayLocked(const QString& method, const QString& key, const QJsonObject& request, const QString& operationId) const
{
    if (!m_owner) return failure("RECOVERY_REQUIRED", "Operation storage is unavailable.");
    if (!safeKey(key) || method.isEmpty() || method.size() > 64 || (!operationId.isEmpty() && !validId(operationId)))
        return failure("INVALID_REQUEST", "Invalid operation identity.");
    const auto requestHash = digest(canonical({{"method", method}, {"request", request}}));
    for (const auto& record : m_records) {
        for (const auto& value : record["requests"].toArray()) {
            const auto earlier = value.toObject();
            if (earlier["idempotencyKey"] != key) continue;
            if (earlier["requestHash"] != requestHash || (!operationId.isEmpty() && record["operationId"] != operationId))
                return failure("IDEMPOTENCY_CONFLICT", "The key already belongs to a different request.");
            auto result = status(record); result["replayed"] = true;
            return {{"result", result}};
        }
    }
    return {};
}
QJsonObject OperationStore::begin(const QString& method, const QString& key, const QJsonObject& request, const QString& operationId)
{
    QMutexLocker lock(&m_mutex);
    const auto previous = replayLocked(method, key, request, operationId);
    if (!previous.isEmpty()) return previous;
    const auto requestHash = digest(canonical({{"method", method}, {"request", request}}));
    QJsonObject record;
    if (!operationId.isEmpty()) {
        if (!m_records.contains(operationId)) return failure("NOT_FOUND", "No such operation.");
        record = m_records.value(operationId);
        if ((method == "recovery.run" && record["state"] != "recovery_required") ||
            (method != "recovery.run" && record["state"] != "prepared")) return failure("INVALID_STATE", "Operation cannot accept this authorization.");
    } else {
        record = {{"format", 1}, {"operationId", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"method", method},
                  {"state", "queued"}, {"createdAt", QString::number(QDateTime::currentMSecsSinceEpoch())}, {"requests", QJsonArray{}}};
    }
    auto requests = record["requests"].toArray();
    requests.append(QJsonObject{{"method", method}, {"idempotencyKey", key}, {"requestHash", requestHash}});
    record["requests"] = requests;
    QString error;
    if (!save(record, &error)) return failure("IO_ERROR", error);
    auto result = status(record); result["replayed"] = false;
    return {{"result", result}};
}
QJsonObject OperationStore::get(const QString& id) const
{
    QMutexLocker lock(&m_mutex);
    if (!m_owner) return failure("RECOVERY_REQUIRED", "Operation storage is unavailable.");
    if (!validId(id)) return failure("INVALID_REQUEST", "Invalid operationId.");
    if (!m_records.contains(id)) return failure("NOT_FOUND", "No such operation.");
    return {{"result", status(m_records.value(id))}};
}
QJsonArray OperationStore::all() const
{
    QMutexLocker lock(&m_mutex); QJsonArray result;
    for (const auto& record : m_records) result.append(status(record));
    return result;
}
bool OperationStore::hasRequest(const QString& id, const QString& method) const
{
    QMutexLocker lock(&m_mutex);
    for (const auto& value : m_records.value(id)["requests"].toArray()) if (value.toObject()["method"] == method) return true;
    return false;
}
QJsonObject OperationStore::note(const QString& id, const QJsonObject& progress, bool cancellation)
{
    QMutexLocker lock(&m_mutex);
    if (!m_owner || !m_records.contains(id)) return failure("NOT_FOUND", "No such operation.");
    auto record = m_records.value(id);
    if (terminal(record["state"].toString())) return failure("INVALID_STATE", "Operation is terminal.");
    if (!progress.isEmpty()) record["progress"] = progress;
    if (cancellation) record["cancellationRequested"] = true;
    QString error;
    return save(record, &error) ? QJsonObject{{"result", status(record)}} : failure("IO_ERROR", error);
}
QJsonObject OperationStore::transition(const QString& id, const QString& state, const QJsonObject& result, const QJsonObject& operationError)
{
    QMutexLocker lock(&m_mutex);
    if (!m_owner) return failure("RECOVERY_REQUIRED", "Operation storage is unavailable.");
    if (!m_records.contains(id)) return failure("NOT_FOUND", "No such operation.");
    auto record = m_records.value(id);
    if (!allowed(record["state"].toString(), state, record["method"].toString())) return failure("INVALID_STATE", "Invalid operation state transition.");
    record["state"] = state;
    if (state == "completed") record.remove("error");
    if (!result.isEmpty()) record["result"] = result;
    if (!operationError.isEmpty()) record["error"] = operationError;
    QString error;
    if (!save(record, &error)) return failure("IO_ERROR", error);
    return {{"result", status(record)}};
}
bool OperationStore::terminal(const QString& state)
{
    return state == "completed" || state == "cancelled" || state == "failed" || state == "rolled_back";
}
}  // namespace Awake::Integration
