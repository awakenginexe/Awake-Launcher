// SPDX-License-Identifier: GPL-3.0-only
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QScopeGuard>
#include <aclapi.h>
#include <qt_windows.h>
#include "awake/integration/OperationStore.h"
#include "awake/integration/DurableIO.h"
#include "awake/integration/ProfileAccess.h"
using namespace Awake::Integration;
class AwakeOperationsTest : public QObject {
    Q_OBJECT
    static bool publicRead(const QString& directory)
    {
        BYTE everyone[SECURITY_MAX_SID_SIZE]; DWORD size = sizeof(everyone);
        if (!CreateWellKnownSid(WinWorldSid, nullptr, everyone, &size)) return false;
        const auto path = QDir::toNativeSeparators(directory);
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        PACL oldAcl = nullptr, newAcl = nullptr;
        if (GetNamedSecurityInfoW(const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(path.utf16())), SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION, nullptr, nullptr, &oldAcl, nullptr, &descriptor) != ERROR_SUCCESS) return false;
        auto cleanup = qScopeGuard([&] { LocalFree(descriptor); if (newAcl) LocalFree(newAcl); });
        EXPLICIT_ACCESSW access{};
        access.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
        access.grfAccessMode = GRANT_ACCESS;
        access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(everyone);
        return SetEntriesInAclW(1, &access, oldAcl, &newAcl) == ERROR_SUCCESS &&
            SetNamedSecurityInfoW(const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(path.utf16())), SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, newAcl, nullptr) == ERROR_SUCCESS;
    }
   private slots:
    void recoveryStorageMustRejectAnExistingPublicReadDirectory()
    {
        QTemporaryDir temporary;
        const auto path = temporary.path() + "/operations";
        QVERIFY(QDir().mkpath(path));
        QVERIFY(publicRead(path));
        QVERIFY(profileAccessIsPrivate(path));
        OperationStore store(path);
        QVERIFY(!store.open());
        QVERIFY(store.begin("plans.prepare", "fixture", {}).contains("error"));
    }
    void newRecoveryStorageDoesNotInheritPublicReadAccess()
    {
        QTemporaryDir temporary;
        QVERIFY(publicRead(temporary.path()));
        QVERIFY(profileAccessIsPrivate(temporary.path()));
        QVERIFY(!storageAccessIsPrivate(temporary.path()));
        OperationStore store(temporary.path() + "/operations");
        QVERIFY(store.open());
        QVERIFY(storageAccessIsPrivate(store.directory()));
        const auto id = store.begin("plans.prepare", "fixture", {})["result"].toObject()["operationId"].toString();
        QVERIFY(!id.isEmpty());
        QVERIFY(storageAccessIsPrivate(store.directory() + '/' + id + ".json"));
        QVERIFY(!storageAccessIsPrivate(temporary.path()));
    }
    void publiclyReadableRecoveryRecordFailsClosed()
    {
        QTemporaryDir temporary;
        QString id;
        {
            OperationStore store(temporary.path() + "/operations"); QVERIFY(store.open());
            id = store.begin("plans.prepare", "fixture", {})["result"].toObject()["operationId"].toString();
        }
        const auto record = temporary.path() + "/operations/" + id + ".json";
        QVERIFY(publicRead(record));
        OperationStore reopened(temporary.path() + "/operations");
        QVERIFY(!reopened.open());
        QVERIFY(QFile::exists(record));
    }
    void acceptanceIsDurableAndIdempotent()
    {
        QTemporaryDir temporary;
        QString id;
        {
            OperationStore store(temporary.path() + "/operations");
            QString error;
            QVERIFY2(store.open(&error), qPrintable(error));
            auto receipt = store.begin("plans.prepare", "key-1", {{"physicalId", "fixture"}})["result"].toObject();
            id = receipt["operationId"].toString();
            QVERIFY(!id.isEmpty());
            QCOMPARE(receipt["state"].toString(), QString("queued"));
            QVERIFY(!receipt["terminal"].toBool());
            QVERIFY(!receipt["replayed"].toBool());
            const auto again = store.begin("plans.prepare", "key-1", {{"physicalId", "fixture"}})["result"].toObject();
            QCOMPARE(again["operationId"].toString(), id);
            QVERIFY(again["replayed"].toBool());
            QCOMPARE(store.begin("plans.prepare", "key-1", {{"physicalId", "different"}})["error"].toObject()["code"].toString(),
                     QString("IDEMPOTENCY_CONFLICT"));
        }
        OperationStore reopened(temporary.path() + "/operations");
        QVERIFY(reopened.open());
        QCOMPARE(reopened.begin("plans.prepare", "key-1", {{"physicalId", "fixture"}})["result"].toObject()["operationId"].toString(), id);
        QCOMPARE(reopened.get(id)["result"].toObject()["state"].toString(), QString("queued"));
    }
    void statesAndApplyAliasSurviveRestart()
    {
        QTemporaryDir temporary;
        QString id;
        {
            OperationStore store(temporary.path() + "/operations");
            QVERIFY(store.open());
            id = store.begin("plans.prepare", "prepare", {})["result"].toObject()["operationId"].toString();
            QCOMPARE(store.transition(id, "completed")["error"].toObject()["code"].toString(), QString("INVALID_STATE"));
            for (const auto* state : {"resolving", "downloading", "prepared"})
                QCOMPARE(store.transition(id, state)["result"].toObject()["state"].toString(), QString(state));
            const auto apply = store.begin("transactions.apply", "apply", {{"planDigest", "exact-plan"}}, id)["result"].toObject();
            QCOMPARE(apply["operationId"].toString(), id);
            QVERIFY(!apply["replayed"].toBool());
            for (const auto* state : {"acquiring", "revalidating", "backing_up", "applying", "verifying", "completed"})
                QCOMPARE(store.transition(id, state, {{"verified", true}})["result"].toObject()["state"].toString(), QString(state));
            QVERIFY(store.get(id)["result"].toObject()["terminal"].toBool());
            QVERIFY(store.transition(id, "applying").contains("error"));
        }
        OperationStore reopened(temporary.path() + "/operations");
        QVERIFY(reopened.open());
        auto receipt = reopened.begin("transactions.apply", "apply", {{"planDigest", "exact-plan"}}, id)["result"].toObject();
        QVERIFY(receipt["replayed"].toBool());
        QCOMPARE(receipt["state"].toString(), QString("completed"));
        QVERIFY(receipt["result"].toObject()["verified"].toBool());
    }
    void invalidKeysAndCorruptRecordsFailClosed()
    {
        QTemporaryDir temporary;
        QString id;
        {
            OperationStore store(temporary.path() + "/operations");
            QVERIFY(store.open());
            for (const auto& key : {QString(), QString("../escape"), QString("valid\n"), QString(129, 'x')})
                QVERIFY(store.begin("plans.prepare", key, {}).contains("error"));
            QVERIFY(store.get("../escape").contains("error"));
            id = store.begin("plans.prepare", "fixture", {})["result"].toObject()["operationId"].toString();
        }
        const auto path = temporary.path() + "/operations/" + id + ".json";
        QFile corrupt(path);
        QVERIFY(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
        corrupt.write("{\"torn\":"); corrupt.close();
        OperationStore reopened(temporary.path() + "/operations");
        QString error;
        QVERIFY(!reopened.open(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(QFile::exists(path));
        QVERIFY(reopened.begin("plans.prepare", "fixture", {}).contains("error"));
    }
    void lockedRecordCannotBecomeACompletedOperation()
    {
        QTemporaryDir temporary;
        OperationStore store(temporary.path() + "/operations");
        QVERIFY(store.open());
        const auto id = store.begin("plans.prepare", "fixture", {})["result"].toObject()["operationId"].toString();
        const auto path = QDir::toNativeSeparators(temporary.path() + "/operations/" + id + ".json");
        const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        QVERIFY(handle != INVALID_HANDLE_VALUE);
        const auto failure = store.transition(id, "resolving");
        CloseHandle(handle);
        QCOMPARE(failure["error"].toObject()["code"].toString(), QString("IO_ERROR"));
        QCOMPARE(store.get(id)["result"].toObject()["state"].toString(), QString("queued"));
        QCOMPARE(QDir(store.directory()).entryList({"*.tmp-*"}, QDir::Files).size(), 0);
        QVERIFY(!store.transition(id, "resolving").contains("error"));
    }
    void exclusiveStoreOwnership()
    {
        QTemporaryDir temporary;
        OperationStore first(temporary.path() + "/operations"), second(temporary.path() + "/operations");
        QVERIFY(first.open());
        QVERIFY(!second.open());
        QVERIFY(second.begin("plans.prepare", "fixture", {}).contains("error"));
    }
    void applyFailureRequiresRecoveryBeforeTerminalState()
    {
        QTemporaryDir temporary;
        OperationStore store(temporary.path() + "/operations");
        QVERIFY(store.open());
        const auto id = store.begin("plans.prepare", "fixture", {})["result"].toObject()["operationId"].toString();
        for (const auto* state : {"resolving", "downloading", "prepared", "acquiring", "revalidating", "backing_up", "applying"})
            QVERIFY(!store.transition(id, state).contains("error"));
        QVERIFY(store.transition(id, "failed").contains("error"));
        QVERIFY(store.transition(id, "cancelled").contains("error"));
        QVERIFY(!store.transition(id, "recovery_required").contains("error"));
        QVERIFY(!store.get(id)["result"].toObject()["terminal"].toBool());
        QVERIFY(!store.transition(id, "rolling_back").contains("error"));
        QVERIFY(!store.transition(id, "recovery_required").contains("error"));
        QVERIFY(!store.transition(id, "rolling_back").contains("error"));
        QVERIFY(!store.transition(id, "rolled_back").contains("error"));
        QVERIFY(store.get(id)["result"].toObject()["terminal"].toBool());
    }
    void durableFilesPreserveHardLinksAndVerifyCopies()
    {
        QTemporaryDir temporary;
        const auto source = temporary.path() + "/original.jar", backup = temporary.path() + "/backup.jar";
        QVERIFY(DurableIO::write(source, "real file bytes"));
        const auto originalHash = DurableIO::hash(source);
        QCOMPARE(originalHash.size(), 64);
        QVERIFY(DurableIO::copy(source, backup));
        QCOMPARE(DurableIO::hash(backup), originalHash);
        const auto alias = QDir::toNativeSeparators(temporary.path() + "/linked.jar");
        const auto target = QDir::toNativeSeparators(source);
        QVERIFY(CreateHardLinkW(reinterpret_cast<LPCWSTR>(alias.utf16()), reinterpret_cast<LPCWSTR>(target.utf16()), nullptr));
        QVERIFY(!DurableIO::write(source, "must not replace"));
        QVERIFY(!DurableIO::remove(source));
        QVERIFY(QFile::remove(alias));
        QCOMPARE(DurableIO::hash(source), originalHash);
        QVERIFY(DurableIO::remove(source));
        QCOMPARE(DurableIO::hash(source), QString("missing"));
        QCOMPARE(DurableIO::hash(backup), originalHash);
        QVERIFY(!DurableIO::write(temporary.path(), "directory"));
    }
};
QTEST_GUILESS_MAIN(AwakeOperationsTest)
#include "AwakeOperations_test.moc"
