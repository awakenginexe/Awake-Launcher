// SPDX-License-Identifier: GPL-3.0-only
#include <QDir>
#include <QCryptographicHash>
#include "awake/integration/ProfileAccess.h"
#include "IntegrationFixture.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <qt_windows.h>
#include "awake/integration/DurableIO.h"
#include "awake/integration/InstanceDiscovery.h"
#include "awake/integration/GameProcessGroup.h"
#include "awake/integration/OperationStore.h"
#include "awake/integration/TransactionEngine.h"
using namespace Awake::Integration;
namespace {
bool put(const QString& path, const QByteArray& bytes)
{
    return QDir().mkpath(QFileInfo(path).absolutePath()) && DurableIO::write(path, bytes);
}
std::shared_ptr<PhysicalInstanceLease> leaseFor(const QString& root)
{
    return fixtureLease(root, physicalIdentityV2(root), QFileInfo(root).absolutePath() + "/leases");
}
QString operation(OperationStore& store, const QString& key = "fixture")
{
    const auto id = store.begin("plans.prepare", key, {})["result"].toObject()["operationId"].toString();
    store.transition(id, "resolving"); store.transition(id, "downloading");
    QDir().mkpath(store.directory() + '/' + id + "/staging");
    return id;
}
QJsonObject threeFilePlan(OperationStore& store, const QString& root)
{
    if (!put(root + "/instance.cfg", "native config")) return {};
    const auto id = operation(store);
    QList<FileChange> changes;
    for (int index = 0; index < 3; ++index) {
        const auto name = QString::number(index) + ".jar";
        if (!put(root + "/.minecraft/mods/" + name, "old-" + QByteArray::number(index)) ||
            !put(store.directory() + '/' + id + "/staging/" + name, "new-" + QByteArray::number(index))) return {};
        changes.append({".minecraft/mods/" + name, name});
    }
    TransactionEngine engine(store, root, ".minecraft/mods");
    return engine.prepare(id, changes, engine.revision())["result"].toObject();
}
int child(const QStringList& arguments)
{
    QProcess process;
    process.start(QCoreApplication::applicationFilePath(), arguments);
    if (!process.waitForFinished(15000)) { process.kill(); process.waitForFinished(5000); return -1; }
    const auto diagnostic = process.readAllStandardError() + process.readAllStandardOutput();
    if (!diagnostic.isEmpty()) qWarning().noquote() << diagnostic;
    return process.exitCode();
}
QByteArray bytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool keeperFinished(const QString& registry)
{
    const auto files = QDir(registry + "/games").entryList({"*.done"}, QDir::Files);
    return files.size() == 1 && QFileInfo(registry + "/games/" + files[0]).size() == 36 &&
        !DurableIO::hash(registry + "/games/" + files[0]).isEmpty();
}
}
class AwakeTransactionsTest : public QObject {
    Q_OBJECT
   private slots:
    void sameTargetEditsArePreservedDuringReplacementAndRemoval_data()
    {
        QTest::addColumn<bool>("removal"); QTest::addColumn<QString>("point");
        QTest::newRow("replace") << false << QString("before_replace");
        QTest::newRow("remove") << true << QString("before_replace");
        QTest::newRow("after-copy") << false << QString("before_exchange");
        QTest::newRow("occupied-gap") << false << QString("after_displace");
    }
    void sameTargetEditsArePreservedDuringReplacementAndRemoval()
    {
        QFETCH(bool, removal);
        QFETCH(QString, point);
        {
            QTemporaryDir temporary; const auto root = temporary.path() + "/instance";
            OperationStore store(temporary.path() + "/operations"); QVERIFY(store.open());
            auto plan = threeFilePlan(store, root);
            if (removal) {
                const auto id = operation(store, "removal"); TransactionEngine engine(store, root, ".minecraft/mods");
                plan = engine.prepare(id, {{".minecraft/mods/0.jar", {}}}, engine.revision())["result"].toObject();
            }
            auto lease = leaseFor(root); QVERIFY(lease); TransactionEngine engine(store, root, ".minecraft/mods");
            const auto result = engine.apply(plan["operationId"].toString(), plan["planDigest"].toString(), plan["instanceRevision"].toString(), lease,
                [&](const QString& phase, int index) { return phase != point || index != 0 || put(root + "/.minecraft/mods/0.jar", "user edit"); });
            QVERIFY(result.contains("error"));
            QCOMPARE(bytes(root + "/.minecraft/mods/0.jar"), QByteArray("user edit"));
            QCOMPARE(store.get(plan["operationId"].toString())["result"].toObject()["state"].toString(), QString("recovery_required"));
        }
    }
    void windowsEquivalentAndShortAliasTargetsAreNotDistinctFiles()
    {
        QTemporaryDir temporary; const auto root = temporary.path() + "/instance";
        QVERIFY(put(root + "/instance.cfg", "fixture"));
        const auto filename = QString("Long existing manually installed artifact.jar");
        const auto path = root + "/.minecraft/mods/" + filename;
        QVERIFY(put(path, "preserve unknown bytes"));
        const auto native = QDir::toNativeSeparators(path);
        const auto needed = GetShortPathNameW(reinterpret_cast<LPCWSTR>(native.utf16()), nullptr, 0);
        QVERIFY(needed);
        QString shortPath; shortPath.resize(int(needed));
        const auto count = GetShortPathNameW(reinterpret_cast<LPCWSTR>(native.utf16()), reinterpret_cast<LPWSTR>(shortPath.data()), needed);
        QVERIFY(count && count < needed); shortPath.truncate(int(count));
        const auto alias = QFileInfo(QDir::fromNativeSeparators(shortPath)).fileName();
        if (alias == filename) QSKIP("8.3 aliases are disabled on this test volume.");
        OperationStore store(temporary.path() + "/operations"); QVERIFY(store.open()); const auto id = operation(store);
        QVERIFY(put(store.directory() + '/' + id + "/staging/artifact", "replacement"));
        TransactionEngine engine(store, root, ".minecraft/mods");
        QVERIFY(engine.prepare(id, {{".minecraft/mods/" + alias, "artifact"}}, engine.revision()).contains("error"));
        QCOMPARE(bytes(path), QByteArray("preserve unknown bytes"));
    }
    void windowsOrdinalAliasesCannotEnterOnePlanTwice()
    {
        QTemporaryDir temporary; const auto root = temporary.path() + "/instance";
        QVERIFY(put(root + "/instance.cfg", "fixture")); QVERIFY(put(root + "/.minecraft/mods/I.jar", "before"));
        const auto other = QString::fromUtf8("\xc4\xb1.jar");
        if (!QFileInfo::exists(root + "/.minecraft/mods/" + other)) QSKIP("This filesystem does not equate these Unicode names.");
        OperationStore store(temporary.path() + "/operations"); QVERIFY(store.open()); const auto id = operation(store);
        QVERIFY(put(store.directory() + '/' + id + "/staging/one", "one")); QVERIFY(put(store.directory() + '/' + id + "/staging/two", "two"));
        TransactionEngine engine(store, root, ".minecraft/mods");
        QVERIFY(engine.prepare(id, {{".minecraft/mods/I.jar", "one"}, {".minecraft/mods/" + other, "two"}}, engine.revision()).contains("error"));
    }
    void lostCanonicalOwnershipCannotBeReenrolledAsANewInstance()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance", registry = temporary.path() + "/leases";
        QVERIFY(put(root + "/instance.cfg", "fixture"));
        const auto id = physicalIdentityV2(root);
        { auto lease = fixtureLease(root, id, registry); QVERIFY(lease); }
        const auto key = QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex());
        QVERIFY(QFile::remove(privateCoordinationBase() + "/authority-v1/" + key + ".json"));
        QVERIFY(!PhysicalInstanceLease::acquire(root, id, registry));
        QVERIFY(!fixtureLease(root, id, registry));
    }
    void unknownOwnershipAndUnsealedGameEvidenceFailClosed()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance", registry = temporary.path() + "/leases";
        QVERIFY(put(root + "/instance.cfg", "fixture"));
        const auto id = physicalIdentityV2(root);
        QVERIFY(!PhysicalInstanceLease::acquire(root, id, registry));
        { auto lease = fixtureLease(root, id, registry); QVERIFY(lease); }
        QVERIFY(InstanceAuthority::requireGameRecord(id));
        QVERIFY(!PhysicalInstanceLease::acquire(root, id, registry));
        auto edited = InstanceAuthority::binding(id);
        edited["gameRecordRequired"] = false;
        const auto key = QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex());
        QVERIFY(put(privateCoordinationBase() + "/authority-v1/" + key + ".json", QJsonDocument(edited).toJson(QJsonDocument::Compact)));
        QVERIFY(!PhysicalInstanceLease::acquire(root, id, registry));
    }
    void differentRegistriesCannotBypassPhysicalExclusion()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance";
        QVERIFY(put(root + "/instance.cfg", "fixture"));
        const auto id = physicalIdentityV2(root);
        auto lease = fixtureLease(root, id, temporary.path() + "/first");
        QVERIFY(lease);
        QCOMPARE(child({"--probe-lease", root, id, temporary.path() + "/second"}), 23);
    }
    void unrelatedEditDuringApplyCannotBecomeTheVerifiedBaseline()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance";
        OperationStore store(temporary.path() + "/operations"); QVERIFY(store.open());
        const auto plan = threeFilePlan(store, root); QVERIFY(!plan.isEmpty());
        auto lease = leaseFor(root); QVERIFY(lease);
        TransactionEngine engine(store, root, ".minecraft/mods");
        const auto result = engine.apply(plan["operationId"].toString(), plan["planDigest"].toString(), plan["instanceRevision"].toString(), lease,
            [&](const QString& phase, int index) {
                if (phase == "after_replace" && index == 1) return put(root + "/.minecraft/mods/manual.jar", "user edit");
                return true;
            });
        QVERIFY(result.contains("error"));
        QCOMPARE(bytes(root + "/.minecraft/mods/manual.jar"), QByteArray("user edit"));
        QCOMPARE(store.get(plan["operationId"].toString())["result"].toObject()["state"].toString(), QString("recovery_required"));
    }
    void keeperRejectsMalformedInvocationAndRequiresVerifiedCompletion()
    {
        QCOMPARE(child({"--integration-game-keeper"}), 61);
        QCOMPARE(child({"--integration-game-keeper", "0", "0", "0", "0", "0", "invalid"}), 62);
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance", registry = temporary.path() + "/leases";
        QVERIFY(put(root + "/instance.cfg", "fixture"));
        const auto id = physicalIdentityV2(root);
        auto lease = fixtureLease(root, id, registry); QVERIFY(lease);
        auto group = GameProcessGroup::create(lease); QVERIFY(group);
        const auto pending = QDir(registry + "/games").entryList({"*.pending"}, QDir::Files);
        QCOMPARE(pending.size(), 1);
        const auto path = registry + "/games/" + pending[0];
        QVERIFY(QDir().mkdir(path.chopped(8)));
        group.reset(); lease.reset();
        QTRY_COMPARE_WITH_TIMEOUT(GameProcessGroup::probe(id, registry), GameProcessGroup::State::Unknown, 5000);
        QCOMPARE(bytes(path).size(), 36);
        QVERIFY(!fixtureLease(root, id, registry));
        QVERIFY(QFileInfo(path.chopped(8)).isDir());
    }
    void gameOwnershipIsDurableAndGuardsOtherProcesses()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance", registry = temporary.path() + "/leases";
        QVERIFY(put(root + "/instance.cfg", "fixture"));
        const auto id = physicalIdentityV2(root);
        QCOMPARE(GameProcessGroup::probe(id, registry), GameProcessGroup::State::Unknown);
        OperationStore store(temporary.path() + "/operations"); QVERIFY(store.open());
        const auto plan = threeFilePlan(store, root); QVERIFY(!plan.isEmpty());
        TransactionEngine engine(store, root, ".minecraft/mods");
        auto lease = fixtureLease(root, id, registry); QVERIFY(lease);
        QString error;
        auto group = GameProcessGroup::create(lease, &error); QVERIFY2(group, qPrintable(error));
        QCOMPARE(GameProcessGroup::probe(id, registry), GameProcessGroup::State::Inactive);
        QProcess process;
        QVERIFY(group->attach(process, &error));
        process.start(QCoreApplication::applicationFilePath(), {"--echo-game"});
        QVERIFY(process.waitForStarted());
        QCOMPARE(GameProcessGroup::probe(id, registry), GameProcessGroup::State::Running);
        QVERIFY(engine.apply(plan["operationId"].toString(), plan["planDigest"].toString(), plan["instanceRevision"].toString(), lease).contains("error"));
        QVERIFY(!group->attach(process));
        process.write("fixture input\n");
        QVERIFY(process.waitForFinished(5000)); QCOMPARE(process.exitCode(), 0);
        QCOMPARE(process.readAllStandardOutput().replace("\r\n", "\n"), QByteArray("fixture input\n"));
        QCOMPARE(GameProcessGroup::probe(id, registry), GameProcessGroup::State::Inactive);
        process.setCreateProcessArgumentsModifier({});
        group.reset(); lease.reset();
        QTRY_VERIFY_WITH_TIMEOUT(keeperFinished(registry), 5000);
        QVERIFY(fixtureLease(root, id, registry));
        const auto records = QDir(registry + "/games").entryList({"*.json"}, QDir::Files);
        QCOMPARE(records.size(), 1);
        QVERIFY(put(registry + "/games/" + records[0], "corrupt ownership"));
        QCOMPARE(GameProcessGroup::probe(id, registry), GameProcessGroup::State::Unknown);
        QVERIFY(!fixtureLease(root, id, registry));
    }
    void launcherCrashDoesNotReleaseRunningDescendants()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance", registry = temporary.path() + "/leases";
        QVERIFY(put(root + "/instance.cfg", "fixture"));
        const auto id = physicalIdentityV2(root);
        const auto nonce = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto stopName = "Local\\AwakeFixtureStop-" + nonce, readyName = "Local\\AwakeFixtureReady-" + nonce;
        const auto stop = CreateEventW(nullptr, TRUE, FALSE, reinterpret_cast<LPCWSTR>(stopName.utf16())); QVERIFY(stop);
        const auto ready = CreateEventW(nullptr, TRUE, FALSE, reinterpret_cast<LPCWSTR>(readyName.utf16())); QVERIFY(ready);
        auto cleanup = qScopeGuard([&] { SetEvent(stop); CloseHandle(ready); CloseHandle(stop); });
        QCOMPARE(child({"--crash-game", root, registry, stopName, readyName}), 90);
        QCOMPARE(WaitForSingleObject(ready, 5000), DWORD(WAIT_OBJECT_0));
        QCOMPARE(GameProcessGroup::probe(id, registry), GameProcessGroup::State::Running);
        QVERIFY(!fixtureLease(root, id, registry));
        QCOMPARE(child({"--probe-lease", root, id, registry}), 23);
        QVERIFY(SetEvent(stop));
        QTRY_COMPARE_WITH_TIMEOUT(GameProcessGroup::probe(id, registry), GameProcessGroup::State::Inactive, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(keeperFinished(registry), 5000);
        QVERIFY(fixtureLease(root, id, registry));
    }
    void physicalIdentityV2IncludesVolumeAndFileIdentifiersWithoutChangingV1()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/one", other = temporary.path() + "/two";
        QVERIFY(put(root + "/instance.cfg", "fixture")); QVERIFY(put(other + "/instance.cfg", "fixture"));
        const auto identity = physicalIdentityV2(root);
        QVERIFY(QRegularExpression(QRegularExpression::anchoredPattern("win2:[0-9a-f-]{36}:[0-9a-f]{32}")).match(identity).hasMatch());
        QCOMPARE(identity, physicalIdentityV2(root + "/../one"));
        QVERIFY(identity != physicalIdentityV2(other));
        QVERIFY(physicalIdentityV2(root + "/missing").isEmpty());
        QVERIFY(physicalIdentity(root).startsWith("win:"));
    }
    void physicalLeaseRejectsAliasesAndOtherProcesses()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance";
        QVERIFY(put(root + "/instance.cfg", "fixture"));
        const auto id = physicalIdentityV2(root);
        const auto registry = temporary.path() + "/leases";
        auto lease = fixtureLease(root, id, registry);
        QVERIFY(lease);
        QVERIFY(!fixtureLease(root + "/../instance", id, registry));
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--probe-lease", root, id, registry});
        QVERIFY(child.waitForFinished(5000)); QCOMPARE(child.exitCode(), 23);
        lease.reset();
        child.start(QCoreApplication::applicationFilePath(), {"--probe-lease", root, id, registry});
        QVERIFY(child.waitForFinished(5000)); QCOMPARE(child.exitCode(), 0);
        QVERIFY(!fixtureLease(root, "wrong-physical-id", registry));
    }
    void physicalLeaseFollowsIdentityAcrossRenameAndAllowsOwnerRemoval()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance", moved = temporary.path() + "/renamed";
        QVERIFY(put(root + "/instance.cfg", "fixture"));
        const auto identity = physicalIdentityV2(root), registry = temporary.path() + "/leases";
        auto lease = fixtureLease(root, identity, registry); QVERIFY(lease);
        QVERIFY(QFile::exists(root + "/.awake-integration/ownership.json"));
        QVERIFY(QDir().rename(root, moved));
        QCOMPARE(physicalIdentityV2(moved), identity);
        QVERIFY(!fixtureLease(moved, identity, registry));
        lease.reset();
        lease = fixtureLease(moved, identity, registry); QVERIFY(lease);
        QVERIFY(QDir(moved).removeRecursively());
        QVERIFY(!fixtureLease(moved, identity, registry));
    }
    void invalidAndCollidingTargetsNeverProduceAPlan()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance";
        QVERIFY(put(root + "/instance.cfg", "native"));
        OperationStore store(temporary.path() + "/operations"); QVERIFY(store.open());
        const auto id = operation(store);
        QVERIFY(put(store.directory() + '/' + id + "/staging/artifact", "new"));
        TransactionEngine engine(store, root, ".minecraft/mods");
        const auto baseline = engine.revision();
        for (const auto& path : {QString("../escape"), QString(".minecraft/mods/CON.jar"), QString(".minecraft/mods/bad.jar:stream"),
                                 QString(".minecraft/mods/bad.jar "), QString("unrelated-user-file")})
            QVERIFY(engine.prepare(id, {{path, "artifact"}}, baseline).contains("error"));
        QVERIFY(engine.prepare(id, {{".minecraft/mods/mod.jar", "../artifact"}}, baseline).contains("error"));
        QVERIFY(engine.prepare(id, {{".minecraft/mods/mod.jar", "artifact"}, {".minecraft/mods/MOD.jar", "artifact"}}, baseline).contains("error"));
        QCOMPARE(store.get(id)["result"].toObject()["state"].toString(), QString("downloading"));
        QCOMPARE(engine.revision(), baseline);
        QVERIFY(!QFile::exists(temporary.path() + "/escape"));
    }
    void nativeConfigurationCanBeReplacedButNotRemoved()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance";
        QVERIFY(put(root + "/instance.cfg", "original config"));
        OperationStore store(temporary.path() + "/operations"); QVERIFY(store.open());
        const auto id = operation(store);
        QVERIFY(put(store.directory() + '/' + id + "/staging/config", "new config"));
        TransactionEngine engine(store, root, ".minecraft/mods");
        const auto revision = engine.revision();
        QVERIFY(engine.prepare(id, {{"instance.cfg", {}}}, revision).contains("error"));
        const auto plan = engine.prepare(id, {{"instance.cfg", "config"}}, revision)["result"].toObject(); QVERIFY(!plan.isEmpty());
        auto lease = leaseFor(root); QVERIFY(lease);
        QCOMPARE(engine.apply(id, plan["planDigest"].toString(), revision, lease)["result"].toObject()["state"].toString(), QString("verifying"));
        QCOMPARE(engine.complete(id, lease)["result"].toObject()["state"].toString(), QString("completed"));
        QCOMPARE(bytes(root + "/instance.cfg"), QByteArray("new config"));
    }
    void terminalRecoveryMustVerifyBytesBeforeClearingARetainedMarker()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance";
        OperationStore store(temporary.path() + "/operations"); QVERIFY(store.open());
        const auto plan = threeFilePlan(store, root); QVERIFY(!plan.isEmpty());
        const auto id = plan["operationId"].toString();
        TransactionEngine engine(store, root, ".minecraft/mods");
        auto lease = leaseFor(root); QVERIFY(lease);
        QCOMPARE(engine.apply(id, plan["planDigest"].toString(), plan["instanceRevision"].toString(), lease)["result"].toObject()["state"].toString(), QString("verifying"));
        const auto marker = QDir::toNativeSeparators(root + "/.awake-integration/recovery.json");
        const auto locked = CreateFileW(reinterpret_cast<LPCWSTR>(marker.utf16()), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr); QVERIFY(locked != INVALID_HANDLE_VALUE);
        const auto completed = engine.complete(id, lease);
        CloseHandle(locked);
        QVERIFY(completed.contains("error"));
        QCOMPARE(store.get(id)["result"].toObject()["state"].toString(), QString("completed"));
        QVERIFY(put(root + "/.minecraft/mods/0.jar", "manual edit"));
        QCOMPARE(engine.recover(id, lease)["error"].toObject()["code"].toString(), QString("RECOVERY_REQUIRED"));
        QVERIFY(QFile::exists(marker));
        QCOMPARE(bytes(root + "/.minecraft/mods/0.jar"), QByteArray("manual edit"));
        QVERIFY(put(root + "/.minecraft/mods/0.jar", "new-0"));
        QCOMPARE(engine.recover(id, lease)["result"].toObject()["state"].toString(), QString("completed"));
        QVERIFY(!QFile::exists(marker));
    }
    void applyPinsTheInstanceDirectoryUntilVerificationCompletes()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance", moved = temporary.path() + "/renamed";
        OperationStore store(temporary.path() + "/operations"); QVERIFY(store.open());
        const auto plan = threeFilePlan(store, root); QVERIFY(!plan.isEmpty());
        TransactionEngine engine(store, root, ".minecraft/mods");
        auto lease = leaseFor(root); QVERIFY(lease);
        const auto id = plan["operationId"].toString();
        QCOMPARE(engine.apply(id, plan["planDigest"].toString(), plan["instanceRevision"].toString(), lease)["result"].toObject()["state"].toString(), QString("verifying"));
        QVERIFY(!QDir().rename(root, moved));
        QVERIFY(!QDir().rename(root + "/.minecraft/mods", root + "/.minecraft/moved-mods"));
        QCOMPARE(engine.complete(id, lease)["result"].toObject()["state"].toString(), QString("completed"));
        lease.reset();
        QVERIFY(QDir().rename(root, moved));
    }
    void prepareDoesNotTouchLiveFilesAndApplyVerifies305Files()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance";
        QVERIFY(put(root + "/instance.cfg", "native config"));
        QVERIFY(put(root + "/mmc-pack.json", "native components"));
        QVERIFY(put(root + "/.minecraft/mods/manual.jar", "keep unknown file"));
        OperationStore store(temporary.path() + "/operations"); QVERIFY(store.open());
        const auto id = operation(store);
        QList<FileChange> changes;
        for (int index = 0; index < 305; ++index) {
            const auto name = QString("mod-%1.jar").arg(index, 3, 10, QChar('0'));
            QVERIFY(put(root + "/.minecraft/mods/" + name, "old bytes"));
            QVERIFY(put(store.directory() + '/' + id + "/staging/" + name, "new bytes"));
            changes.append({".minecraft/mods/" + name, name});
        }
        TransactionEngine engine(store, root, ".minecraft/mods");
        const auto baseline = engine.revision(); QVERIFY(!baseline.isEmpty());
        const auto plan = engine.prepare(id, changes, baseline)["result"].toObject();
        QCOMPARE(plan["files"].toArray().size(), 305);
        QVERIFY(!plan["planDigest"].toString().isEmpty());
        QVERIFY(plan["requiredDiskBytes"].toString().toULongLong() > 0);
        QCOMPARE(bytes(root + "/.minecraft/mods/mod-000.jar"), QByteArray("old bytes"));
        QVERIFY(engine.apply(id, plan["planDigest"].toString(), baseline, {}).contains("error"));
        auto lease = leaseFor(root); QVERIFY(lease);
        const auto outcome = engine.apply(id, plan["planDigest"].toString(), baseline, lease);
        QCOMPARE(outcome["result"].toObject()["state"].toString(), QString("verifying"));
        QVERIFY(!outcome["result"].toObject()["terminal"].toBool());
        QCOMPARE(engine.complete(id, lease)["result"].toObject()["state"].toString(), QString("completed"));
        for (const auto& change : changes)
            QCOMPARE(DurableIO::hash(root + '/' + change.relativePath), DurableIO::hash(store.directory() + '/' + id + "/staging/" + change.stagedName));
        QFile manual(root + "/.minecraft/mods/manual.jar"); QVERIFY(manual.open(QIODevice::ReadOnly)); QCOMPARE(manual.readAll(), QByteArray("keep unknown file"));
        QVERIFY(!QFile::exists(root + "/.awake-integration/recovery.json"));
        QCOMPARE(engine.recover(id, lease)["result"].toObject()["state"].toString(), QString("completed"));
    }
    void stalePlanAndTamperedStagingNeverModifyLiveFiles()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance";
        QVERIFY(put(root + "/instance.cfg", "native config"));
        QVERIFY(put(root + "/.minecraft/mods/mod.jar", "old"));
        OperationStore store(temporary.path() + "/operations"); QVERIFY(store.open());
        const auto id = operation(store);
        QVERIFY(put(store.directory() + '/' + id + "/staging/artifact", "new"));
        TransactionEngine engine(store, root, ".minecraft/mods");
        const auto baseline = engine.revision();
        const auto plan = engine.prepare(id, {{".minecraft/mods/mod.jar", "artifact"}}, baseline)["result"].toObject();
        auto lease = leaseFor(root); QVERIFY(lease);
        QCOMPARE(engine.apply(id, "wrong-digest", baseline, lease)["error"].toObject()["code"].toString(), QString("STALE_PLAN"));
        QVERIFY(put(store.directory() + '/' + id + "/staging/artifact", "tampered"));
        QCOMPARE(engine.apply(id, plan["planDigest"].toString(), baseline, lease)["error"].toObject()["code"].toString(), QString("INTEGRITY_FAILED"));
        QFile live(root + "/.minecraft/mods/mod.jar"); QVERIFY(live.open(QIODevice::ReadOnly)); QCOMPARE(live.readAll(), QByteArray("old"));
        QVERIFY(!QFile::exists(root + "/.awake-integration/recovery.json"));
        QVERIFY(put(store.directory() + '/' + id + "/staging/artifact", "new"));
        QVERIFY(put(root + "/instance.cfg", "new native settings"));
        QCOMPARE(engine.apply(id, plan["planDigest"].toString(), baseline, lease)["error"].toObject()["code"].toString(), QString("STALE_INSTANCE"));
    }
    void deletionAndDisableAreReviewedFileChanges()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance";
        QVERIFY(put(root + "/instance.cfg", "native"));
        QVERIFY(put(root + "/.minecraft/mods/remove.jar", "remove"));
        QVERIFY(put(root + "/.minecraft/mods/disable.jar", "disable"));
        OperationStore store(temporary.path() + "/operations"); QVERIFY(store.open());
        const auto id = operation(store);
        QVERIFY(put(store.directory() + '/' + id + "/staging/disabled", "disable"));
        TransactionEngine engine(store, root, ".minecraft/mods");
        const auto baseline = engine.revision();
        const auto plan = engine.prepare(id, {{".minecraft/mods/remove.jar", {}}, {".minecraft/mods/disable.jar", {}},
                                              {".minecraft/mods/disable.jar.disabled", "disabled"}}, baseline)["result"].toObject();
        auto lease = leaseFor(root); QVERIFY(lease);
        QCOMPARE(engine.apply(id, plan["planDigest"].toString(), baseline, lease)["result"].toObject()["state"].toString(), QString("verifying"));
        QCOMPARE(engine.complete(id, lease)["result"].toObject()["state"].toString(), QString("completed"));
        QVERIFY(!QFile::exists(root + "/.minecraft/mods/remove.jar"));
        QVERIFY(!QFile::exists(root + "/.minecraft/mods/disable.jar"));
        QVERIFY(QFile::exists(root + "/.minecraft/mods/disable.jar.disabled"));
    }
    void interruptedApplyAndRecoveryRestoreExactBytes()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance", storage = temporary.path() + "/operations";
        QJsonObject plan;
        {
            OperationStore store(storage); QVERIFY(store.open());
            plan = threeFilePlan(store, root); QVERIFY(!plan.isEmpty());
        }
        const auto id = plan["operationId"].toString();
        QCOMPARE(child({"--crash-apply", storage, root, id, plan["planDigest"].toString(), plan["instanceRevision"].toString()}), 86);
        QCOMPARE(bytes(root + "/.minecraft/mods/0.jar"), QByteArray("new-0"));
        QCOMPARE(bytes(root + "/.minecraft/mods/1.jar"), QByteArray("new-1"));
        QVERIFY(QFile::exists(root + "/.awake-integration/recovery.json"));
        QCOMPARE(child({"--crash-recover", storage, root, id}), 87);
        QCOMPARE(bytes(root + "/.minecraft/mods/0.jar"), QByteArray("new-0"));
        QCOMPARE(bytes(root + "/.minecraft/mods/1.jar"), QByteArray("old-1"));
        OperationStore reopened(storage); QVERIFY(reopened.open());
        TransactionEngine engine(reopened, root, ".minecraft/mods");
        auto lease = leaseFor(root); QVERIFY(lease);
        const auto restored = engine.recover(id, lease);
        QCOMPARE(restored["result"].toObject()["state"].toString(), QString("rolled_back"));
        for (int index = 0; index < 3; ++index)
            QCOMPARE(bytes(root + "/.minecraft/mods/" + QString::number(index) + ".jar"), "old-" + QByteArray::number(index));
        QCOMPARE(engine.revision(), plan["instanceRevision"].toString());
        QVERIFY(!QFile::exists(root + "/.awake-integration/recovery.json"));
    }
    void diskFullFaultRollsBackAndPersistsTheFailure()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance";
        OperationStore store(temporary.path() + "/operations"); QVERIFY(store.open());
        const auto plan = threeFilePlan(store, root); QVERIFY(!plan.isEmpty());
        TransactionEngine engine(store, root, ".minecraft/mods");
        auto lease = leaseFor(root); QVERIFY(lease);
        const auto id = plan["operationId"].toString();
        const auto result = engine.apply(id, plan["planDigest"].toString(), plan["instanceRevision"].toString(), lease,
            [](const QString& point, int index) {
                if (point == "before_replace" && index == 1) { SetLastError(ERROR_DISK_FULL); return false; }
                return true;
            });
        QCOMPARE(result["error"].toObject()["code"].toString(), QString("IO_ERROR"));
        const auto status = store.get(id)["result"].toObject();
        QCOMPARE(status["state"].toString(), QString("rolled_back"));
        QVERIFY(status["error"].toObject()["message"].toString().contains(QString::number(ERROR_DISK_FULL)));
        QCOMPARE(engine.revision(), plan["instanceRevision"].toString());
        QVERIFY(!QFile::exists(root + "/.awake-integration/recovery.json"));
    }
    void lockedRollbackRetainsRecoveryUntilTheLockIsReleased()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance";
        OperationStore store(temporary.path() + "/operations"); QVERIFY(store.open());
        const auto plan = threeFilePlan(store, root); QVERIFY(!plan.isEmpty());
        TransactionEngine engine(store, root, ".minecraft/mods");
        auto lease = leaseFor(root); QVERIFY(lease);
        const auto id = plan["operationId"].toString();
        HANDLE locked = INVALID_HANDLE_VALUE;
        auto unlock = qScopeGuard([&] { if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked); });
        const auto result = engine.apply(id, plan["planDigest"].toString(), plan["instanceRevision"].toString(), lease,
            [&](const QString& point, int index) {
                if (point == "after_replace" && index == 0) {
                    const auto path = QDir::toNativeSeparators(root + "/.minecraft/mods/1.jar");
                    locked = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
                }
                return true;
            });
        QVERIFY(locked != INVALID_HANDLE_VALUE);
        QCOMPARE(result["error"].toObject()["code"].toString(), QString("RECOVERY_REQUIRED"));
        QCOMPARE(store.get(id)["result"].toObject()["state"].toString(), QString("recovery_required"));
        QVERIFY(QFile::exists(root + "/.awake-integration/recovery.json"));
        QVERIFY(QFile::exists(store.directory() + '/' + id + "/backups/0"));
        CloseHandle(locked); locked = INVALID_HANDLE_VALUE;
        QCOMPARE(engine.recover(id, lease)["result"].toObject()["state"].toString(), QString("rolled_back"));
        QCOMPARE(engine.revision(), plan["instanceRevision"].toString());
    }
    void unexpectedBytesArePreservedDuringRollback()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance";
        OperationStore store(temporary.path() + "/operations"); QVERIFY(store.open());
        const auto plan = threeFilePlan(store, root); QVERIFY(!plan.isEmpty());
        TransactionEngine engine(store, root, ".minecraft/mods");
        auto lease = leaseFor(root); QVERIFY(lease);
        const auto id = plan["operationId"].toString();
        const auto result = engine.apply(id, plan["planDigest"].toString(), plan["instanceRevision"].toString(), lease,
            [&](const QString& point, int index) {
                if (point == "after_replace" && index == 0) {
                    put(root + "/.minecraft/mods/0.jar", "unrecognized user edit");
                    return false;
                }
                return true;
            });
        QCOMPARE(result["error"].toObject()["code"].toString(), QString("RECOVERY_REQUIRED"));
        QCOMPARE(bytes(root + "/.minecraft/mods/0.jar"), QByteArray("unrecognized user edit"));
        QVERIFY(QFile::exists(store.directory() + '/' + id + "/backups/0"));
        QVERIFY(QFile::exists(root + "/.awake-integration/recovery.json"));
        QVERIFY(put(root + "/.minecraft/mods/0.jar", "new-0"));
        QCOMPARE(engine.recover(id, lease)["error"].toObject()["code"].toString(), QString("RECOVERY_REQUIRED"));
        QCOMPARE(bytes(root + "/.minecraft/mods/0.jar"), QByteArray("new-0"));
    }
    void corruptJournalBlocksRecoveryWithoutDeletingEvidence()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance", storage = temporary.path() + "/operations";
        QJsonObject plan;
        {
            OperationStore store(storage); QVERIFY(store.open());
            plan = threeFilePlan(store, root); QVERIFY(!plan.isEmpty());
        }
        const auto id = plan["operationId"].toString();
        QCOMPARE(child({"--crash-apply", storage, root, id, plan["planDigest"].toString(), plan["instanceRevision"].toString()}), 86);
        QVERIFY(put(storage + '/' + id + "/journal.json", "torn journal"));
        OperationStore reopened(storage); QVERIFY(reopened.open());
        TransactionEngine engine(reopened, root, ".minecraft/mods");
        auto lease = leaseFor(root); QVERIFY(lease);
        QCOMPARE(engine.recover(id, lease)["error"].toObject()["code"].toString(), QString("RECOVERY_REQUIRED"));
        QCOMPARE(reopened.get(id)["result"].toObject()["state"].toString(), QString("recovery_required"));
        QVERIFY(QFile::exists(storage + '/' + id + "/backups/0"));
        QVERIFY(QFile::exists(root + "/.awake-integration/recovery.json"));
        QCOMPARE(bytes(root + "/.minecraft/mods/0.jar"), QByteArray("new-0"));
    }
    void crashAfterFileVerificationStillRollsBackToTheReviewedBaseline()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance", storage = temporary.path() + "/operations";
        QJsonObject plan;
        {
            OperationStore store(storage); QVERIFY(store.open());
            plan = threeFilePlan(store, root); QVERIFY(!plan.isEmpty());
        }
        const auto id = plan["operationId"].toString();
        QCOMPARE(child({"--crash-verified", storage, root, id, plan["planDigest"].toString(), plan["instanceRevision"].toString()}), 88);
        OperationStore reopened(storage); QVERIFY(reopened.open());
        TransactionEngine engine(reopened, root, ".minecraft/mods");
        auto lease = leaseFor(root); QVERIFY(lease);
        const auto status = engine.recover(id, lease)["result"].toObject();
        QCOMPARE(status["state"].toString(), QString("rolled_back"));
        QCOMPARE(status["result"].toObject()["instanceRevision"].toString(), plan["instanceRevision"].toString());
        QCOMPARE(engine.revision(), plan["instanceRevision"].toString());
    }
    void durableCommitIntentSurvivesACompletionRecordInterruption()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance", storage = temporary.path() + "/operations";
        QJsonObject plan;
        {
            OperationStore store(storage); QVERIFY(store.open());
            plan = threeFilePlan(store, root); QVERIFY(!plan.isEmpty());
        }
        const auto id = plan["operationId"].toString();
        QCOMPARE(child({"--crash-complete", storage, root, id, plan["planDigest"].toString(), plan["instanceRevision"].toString()}), 89);
        OperationStore reopened(storage); QVERIFY(reopened.open());
        QVERIFY(!reopened.transition(id, "recovery_required").contains("error"));
        TransactionEngine engine(reopened, root, ".minecraft/mods");
        auto lease = leaseFor(root); QVERIFY(lease);
        const auto status = engine.recover(id, lease)["result"].toObject();
        QCOMPARE(status["state"].toString(), QString("completed"));
        for (int index = 0; index < 3; ++index)
            QCOMPARE(bytes(root + "/.minecraft/mods/" + QString::number(index) + ".jar"), "new-" + QByteArray::number(index));
        QCOMPARE(status["result"].toObject()["instanceRevision"].toString(), engine.revision());
        QVERIFY(!QFile::exists(root + "/.awake-integration/recovery.json"));
    }
};
int main(int argc, char** argv)
{
    const auto keeper = GameProcessGroup::runKeeper(argc, argv);
    if (keeper >= 0) return keeper;
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.size() == 2 && arguments[1] == "--echo-game") {
        QFile input, output;
        if (!input.open(stdin, QIODevice::ReadOnly) || !output.open(stdout, QIODevice::WriteOnly)) return 45;
        output.write(input.readLine()); output.flush(); return 0;
    }
    if (arguments.size() == 4 && arguments[1] == "--wait-game") {
        const auto stop = OpenEventW(SYNCHRONIZE, FALSE, reinterpret_cast<LPCWSTR>(arguments[2].utf16()));
        const auto ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, reinterpret_cast<LPCWSTR>(arguments[3].utf16()));
        if (!stop || !ready || !SetEvent(ready)) return 46;
        const auto result = WaitForSingleObject(stop, 12000);
        CloseHandle(ready); CloseHandle(stop); return result == WAIT_OBJECT_0 ? 0 : 47;
    }
    if (arguments.size() == 4 && arguments[1] == "--spawn-game") {
        QProcess process;
        process.setProcessChannelMode(QProcess::ForwardedChannels);
        process.start(app.applicationFilePath(), {"--wait-game", arguments[2], arguments[3]});
        if (!process.waitForStarted()) return 48;
        ExitProcess(91);
    }
    if (arguments.size() == 6 && arguments[1] == "--crash-game") {
        auto lease = fixtureLease(arguments[2], physicalIdentityV2(arguments[2]), arguments[3]);
        auto group = GameProcessGroup::create(lease);
        QProcess process;
        process.setProcessChannelMode(QProcess::ForwardedChannels);
        if (!group || !group->attach(process)) return 49;
        process.start(app.applicationFilePath(), {"--spawn-game", arguments[4], arguments[5]});
        if (!process.waitForFinished(5000) || process.exitCode() != 91) return 50;
        const auto ready = OpenEventW(SYNCHRONIZE, FALSE, reinterpret_cast<LPCWSTR>(arguments[5].utf16()));
        if (!ready || WaitForSingleObject(ready, 5000) != WAIT_OBJECT_0) return 51;
        CloseHandle(ready);
        if (GameProcessGroup::probe(lease->physicalId(), lease->registryDirectory()) != GameProcessGroup::State::Running) return 52;
        ExitProcess(90);
    }
    if (arguments.size() == 5 && arguments[1] == "--probe-lease")
        return fixtureLease(arguments[2], arguments[3], arguments[4]) ? 0 : 23;
    if ((arguments.size() == 7 && (arguments[1] == "--crash-apply" || arguments[1] == "--crash-verified" || arguments[1] == "--crash-complete")) ||
        (arguments.size() == 5 && arguments[1] == "--crash-recover")) {
        OperationStore store(arguments[2]);
        if (!store.open()) return 31;
        TransactionEngine engine(store, arguments[3], ".minecraft/mods");
        auto lease = leaseFor(arguments[3]);
        if (!lease) return 32;
        if (arguments[1] != "--crash-recover") {
            const auto result = engine.apply(arguments[4], arguments[5], arguments[6], lease, [&](const QString& point, int index) {
                if (arguments[1] == "--crash-apply" && point == "after_replace" && index == 1) ExitProcess(86);
                return true;
            });
            if (result["result"].toObject()["state"] != "verifying") return 34;
            if (arguments[1] == "--crash-verified") ExitProcess(88);
            engine.complete(arguments[4], lease, [](const QString& point, int) {
                if (point == "after_commit_record") ExitProcess(89);
                return true;
            });
        } else {
            const auto recovered = engine.recover(arguments[4], lease, [](const QString& point, int index) {
                if (point == "after_restore" && index == 1) ExitProcess(87);
                return true;
            });
            QFile diagnostic;
            if (diagnostic.open(stderr, QIODevice::WriteOnly)) {
                diagnostic.write(QJsonDocument(recovered).toJson(QJsonDocument::Compact)); diagnostic.flush();
            }
        }
        return 33;
    }
    AwakeTransactionsTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "AwakeTransactions_test.moc"
