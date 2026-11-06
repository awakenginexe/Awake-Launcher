#include <QTest>
#include <QThread>
#include <QTimer>

#include <tasks/ConcurrentTask.h>
#include <tasks/MultipleOptionsTask.h>
#include <tasks/SequentialTask.h>
#include <tasks/Task.h>
#include <net/NetJob.h>
#include <QNetworkAccessManager>
#include <QSignalSpy>

#include <array>
#include <atomic>
#ifdef Q_OS_WIN
#include <QTemporaryDir>
#include <QDir>
#include "IntegrationFixture.h"
#include "awake/integration/DurableIO.h"
#include "awake/integration/InstanceDiscovery.h"
#include "awake/integration/InstanceOperationScope.h"
using namespace Awake::Integration;
#endif

/* Does nothing. Only used for testing. */
class BasicTask : public Task {
    Q_OBJECT

    friend class TaskTest;

   public:
    BasicTask(bool show_debug_log = true) : Task(show_debug_log) {}

   private:
    void executeTask() override { emitSucceeded(); }
};

/* Does nothing. Only used for testing. */
class BasicTask_MultiStep : public Task {
    Q_OBJECT

    friend class TaskTest;

   private:
    auto isMultiStep() const -> bool override { return true; }

    void executeTask() override {}
};

class FailedAccountRefresh : public Task {
public:
    int attempts = 0;
    void executeTask() override { ++attempts; emitFailed("Account refresh failed"); }
};
#ifdef Q_OS_WIN
class QueuedLeaseTask : public Task {
   public:
    bool synchronous = false, queued = false;
    QString expected;
    std::shared_ptr<BasicTask> child;
    void executeTask() override
    {
        synchronous = InstanceOperationScope::current() && InstanceOperationScope::current()->physicalId() == expected;
        QMetaObject::invokeMethod(this, [this] {
            queued = InstanceOperationScope::current() && InstanceOperationScope::current()->physicalId() == expected;
            child = std::make_shared<BasicTask>();
            emitSucceeded();
        }, Qt::QueuedConnection);
    }
};
class ReentrantLeaseTask : public Task {
   public:
    int attempts = 0;
    void executeTask() override
    {
        if (++attempts == 1) emitSucceeded();
        else QMetaObject::invokeMethod(this, [this] { emitSucceeded(); }, Qt::QueuedConnection);
    }
};
#endif

class QueuedChildTask : public ConcurrentTask {
    void executeTask() override { ConcurrentTask::executeTask(); executeNextSubTask(); }
};

class BigConcurrentTask : public ConcurrentTask {
    Q_OBJECT

    void executeNextSubTask() override
    {
        // This is here only to help fill the stack a bit more quickly (if there's an issue, of course :^))
        // Each tasks thus adds 1024 * 4 bytes to the stack, at the very least.
        [[maybe_unused]] volatile std::array<uint32_t, 1024> some_data_on_the_stack{};

        ConcurrentTask::executeNextSubTask();
    }
};

class BigConcurrentTaskThread : public QThread {
    Q_OBJECT

    QTimer m_deadline;
    void run() override
    {
        BigConcurrentTask big_task;
        m_deadline.setInterval(10000);

        // NOTE: Arbitrary value that manages to trigger a problem when there is one.
        //       Considering each tasks, in a problematic state, adds 1024 * 4 bytes to the stack,
        //       this number is enough to fill up 16 MiB of stack, more than enough to cause a problem.
        static const unsigned s_num_tasks = 1 << 12;
        for (unsigned i = 0; i < s_num_tasks; i++) {
            auto sub_task = makeShared<BasicTask>(false);
            big_task.addTask(sub_task);
        }

        connect(&big_task, &Task::finished, this, &QThread::quit);
        connect(&m_deadline, &QTimer::timeout, this, [this] {
            passed_the_deadline = true;
            quit();
        });

        if (thread() != QThread::currentThread()) {
            QMetaObject::invokeMethod(this, &BigConcurrentTaskThread::start_timer, Qt::QueuedConnection);
        }
        big_task.run();

        exec();
    }
    void start_timer() { m_deadline.start(); }

   public:
    bool passed_the_deadline = false;
};

class TaskTest : public QObject {
    Q_OBJECT

   private slots:
    void canceledConcurrentParentCanRunNewWork()
    {
        auto oldChild = makeShared<ConcurrentTask>(); oldChild->addTask(makeShared<BasicTask>());
        QueuedChildTask parent; parent.addTask(oldChild); parent.start();
        QVERIFY(parent.abort());
        auto fresh = makeShared<BasicTask>(); parent.addTask(fresh); parent.start();
        QTRY_VERIFY_WITH_TIMEOUT(parent.isFinished(), 1000);
        QVERIFY(parent.wasSuccessful()); QVERIFY(fresh->wasSuccessful());
    }
#ifdef Q_OS_WIN
    void cancelingQueuedConcurrentChildrenReleasesTheirOwnership()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance", registry = temporary.path() + "/leases";
        QVERIFY(QDir().mkpath(root)); QVERIFY(DurableIO::write(root + "/instance.cfg", "fixture"));
        const auto id = physicalIdentityV2(root);
        auto acquire = [&] { return fixtureLease(root, id, registry); };
        std::unique_ptr<ConcurrentTask> parent;
        Task::Ptr child;
        {
            auto lease = acquire(); QVERIFY(lease);
            InstanceOperationScope scope(lease);
            parent = std::make_unique<ConcurrentTask>(); child = makeShared<BasicTask>(); parent->addTask(child);
        }
        parent->start(); QVERIFY(parent->abort());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(parent->getState(), Task::State::AbortedByUser);
        QCOMPARE(child->getState(), Task::State::AbortedByUser);
        QVERIFY(acquire());
    }
    void cancelingQueuedNetworkChildrenReleasesTheirOwnership()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance", registry = temporary.path() + "/leases";
        QVERIFY(QDir().mkpath(root)); QVERIFY(DurableIO::write(root + "/instance.cfg", "fixture"));
        const auto id = physicalIdentityV2(root);
        auto acquire = [&] { return fixtureLease(root, id, registry); };
        QNetworkAccessManager network;
        std::unique_ptr<NetJob> parent;
        Task::Ptr child;
        {
            auto lease = acquire(); QVERIFY(lease);
            InstanceOperationScope scope(lease);
            parent = std::make_unique<NetJob>("Queued ownership fixture", &network, 1);
            child = makeShared<BasicTask>(); parent->addTask(child);
        }
        parent->start(); QVERIFY(parent->abort());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(child->getState(), Task::State::AbortedByUser);
        QVERIFY(acquire());
    }
    void reentrantRestartRetainsTheNewTaskOwnership()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance", registry = temporary.path() + "/leases";
        QVERIFY(QDir().mkpath(root)); QVERIFY(DurableIO::write(root + "/instance.cfg", "fixture"));
        const auto id = physicalIdentityV2(root);
        auto acquire = [&] { return fixtureLease(root, id, registry); };
        std::unique_ptr<ReentrantLeaseTask> task;
        {
            auto lease = acquire(); QVERIFY(lease);
            InstanceOperationScope scope(lease); task = std::make_unique<ReentrantLeaseTask>();
        }
        connect(task.get(), &Task::finished, task.get(), [&] { if (task->attempts == 1) task->start(); });
        task->start(); QVERIFY(task->isRunning()); QCOMPARE(task->attempts, 2);
        QVERIFY(!acquire());
        QCoreApplication::sendPostedEvents(task.get(), QEvent::MetaCall);
        QVERIFY(task->wasSuccessful()); QVERIFY(acquire());
    }
    void taskOwnershipIsScopedToTheExecutingThread()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance";
        QVERIFY(QDir().mkpath(root)); QVERIFY(DurableIO::write(root + "/instance.cfg", "fixture"));
        const auto id = physicalIdentityV2(root);
        auto lease = fixtureLease(root, id, temporary.path() + "/leases"); QVERIFY(lease);
        InstanceOperationScope scope(lease);
        BasicTask task;
        std::atomic_bool observed{false}, restored{false};
        connect(&task, &Task::finished, &task, [&] {
            observed = InstanceOperationScope::current() && InstanceOperationScope::current()->physicalId() == id;
        }, Qt::DirectConnection);
        auto thread = std::unique_ptr<QThread>(QThread::create([&] {
            if (InstanceOperationScope::current()) return;
            task.start();
            restored = !InstanceOperationScope::current();
        }));
        thread->start(); QVERIFY(thread->wait(5000));
        QVERIFY(observed); QVERIFY(restored);
        QCOMPARE(InstanceOperationScope::current()->physicalId(), id);
    }
    void instanceLeaseSurvivesQueuedTaskAndChildLifetimes()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance", registry = temporary.path() + "/leases";
        QVERIFY(QDir().mkpath(root)); QVERIFY(DurableIO::write(root + "/instance.cfg", "fixture"));
        const auto id = physicalIdentityV2(root);
        auto acquire = [&] { return fixtureLease(root, id, registry); };
        std::unique_ptr<QueuedLeaseTask> task;
        {
            auto lease = acquire(); QVERIFY(lease);
            InstanceOperationScope scope(lease);
            task = std::make_unique<QueuedLeaseTask>(); task->expected = id;
        }
        QVERIFY(!InstanceOperationScope::current());
        QVERIFY(!acquire());
        task->start();
        QVERIFY(task->synchronous); QVERIFY(task->isRunning());
        QVERIFY(!InstanceOperationScope::current());
        QCoreApplication::sendPostedEvents(task.get(), QEvent::MetaCall);
        QVERIFY(task->queued); QVERIFY(task->wasSuccessful()); QVERIFY(task->child);
        QVERIFY(!acquire());
        bool childScope = false;
        connect(task->child.get(), &Task::finished, this, [&] {
            childScope = InstanceOperationScope::current() && InstanceOperationScope::current()->physicalId() == id;
        });
        task->child->start(); QVERIFY(task->child->wasSuccessful()); QVERIFY(childScope);
        QVERIFY(!InstanceOperationScope::current());
        QVERIFY(acquire());
    }
    void restartingTaskReacquiresOwnershipAndFailsWhenBusy()
    {
        QTemporaryDir temporary;
        const auto root = temporary.path() + "/instance", registry = temporary.path() + "/leases";
        QVERIFY(QDir().mkpath(root)); QVERIFY(DurableIO::write(root + "/instance.cfg", "fixture"));
        const auto id = physicalIdentityV2(root);
        auto acquire = [&] { return fixtureLease(root, id, registry); };
        std::unique_ptr<BasicTask> task;
        {
            auto lease = acquire(); QVERIFY(lease);
            InstanceOperationScope scope(lease); task = std::make_unique<BasicTask>();
        }
        task->start(); QVERIFY(task->wasSuccessful());
        auto other = acquire(); QVERIFY(other);
        task->start(); QCOMPARE(task->getState(), Task::State::Failed);
        QVERIFY(!task->failReason().isEmpty());
        other.reset(); task->start(); QVERIFY(task->wasSuccessful());
        QVERIFY(acquire());
    }
#endif
    void canceledParentDoesNotStartQueuedChild_data()
    {
        QTest::addColumn<bool>("restart");
        QTest::newRow("cancel") << false;
        QTest::newRow("cancel-then-restart") << true;
    }
    void canceledParentDoesNotStartQueuedChild()
    {
        QFETCH(bool, restart);
        auto child = makeShared<ConcurrentTask>();
        child->addTask(makeShared<BasicTask>());
        QSignalSpy started(child.get(), &Task::started);
        QueuedChildTask parent;
        parent.addTask(child);
        parent.start();
        QVERIFY(parent.abort());
        QVERIFY(!parent.isRunning());
        if (restart) parent.start();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(started.count(), 0);
        if (restart) QVERIFY(parent.abort());
    }
    void canceledNetJobFinishesBeforeQueuedDispatch()
    {
        QNetworkAccessManager network;
        NetJob job("Canceled network dispatch", &network, 1);
        job.setAskRetry(false);
        auto child = makeShared<BasicTask>();
        job.addTask(child);
        QSignalSpy started(child.get(), &Task::started);
        job.start();
        QVERIFY(job.abort());
        QVERIFY(job.isFinished());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(started.count(), 0);
    }
    void netJobReportsFailedAccountTasksWithoutNetworkRetries()
    {
        QNetworkAccessManager network;
        NetJob job("Skin refresh failure", &network, 1);
        job.setAskRetry(false);
        auto refresh = makeShared<FailedAccountRefresh>();
        job.addTask(refresh);
        QSignalSpy failed(&job, &Task::failed);
        job.start();
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(refresh->attempts, 1);
        QCOMPARE(job.failReason(), QString("Account refresh failed"));
        QVERIFY(job.getFailedActions().isEmpty());
        QVERIFY(job.getFailedFiles().isEmpty());
    }
    void test_SetStatus_NoMultiStep()
    {
        BasicTask t;
        QString status{ "test status" };

        t.setStatus(status);

        QCOMPARE(t.getStatus(), status);
        QCOMPARE(t.getStepProgress().isEmpty(), TaskStepProgressList{}.isEmpty());
    }

    void test_SetStatus_MultiStep()
    {
        BasicTask_MultiStep t;
        QString status{ "test status" };

        t.setStatus(status);

        QCOMPARE(t.getStatus(), status);
        // Even though it is multi step, it does not override the getStepStatus method,
        // so it should remain the same.
        QCOMPARE(t.getStepProgress().isEmpty(), TaskStepProgressList{}.isEmpty());
    }

    void test_SetProgress()
    {
        BasicTask t;
        int current = 42;
        int total = 207;

        t.setProgress(current, total);

        QCOMPARE(t.getProgress(), current);
        QCOMPARE(t.getTotalProgress(), total);
    }

    void test_basicRun()
    {
        BasicTask t;
        connect(&t, &Task::finished, &t,
                [&t] { QVERIFY2(t.wasSuccessful(), "Task finished but was not successful when it should have been."); });
        t.start();

        QVERIFY2(QTest::qWaitFor([&t]() { return t.isFinished(); }, 1000), "Task didn't finish as it should.");
    }

    void test_basicConcurrentRun()
    {
        auto t1 = makeShared<BasicTask>();
        auto t2 = makeShared<BasicTask>();
        auto t3 = makeShared<BasicTask>();

        ConcurrentTask t;

        t.addTask(t1);
        t.addTask(t2);
        t.addTask(t3);

        connect(&t, &Task::finished, &t, [&t, &t1, &t2, &t3] {
            QVERIFY2(t.wasSuccessful(), "Task finished but was not successful when it should have been.");
            QVERIFY(t1->wasSuccessful());
            QVERIFY(t2->wasSuccessful());
            QVERIFY(t3->wasSuccessful());
        });

        t.start();
        QVERIFY2(QTest::qWaitFor([&t]() { return t.isFinished(); }, 1000), "Task didn't finish as it should.");
    }

    // Tests if starting new tasks after the 6 initial ones is working
    void test_moreConcurrentRun()
    {
        auto t1 = makeShared<BasicTask>();
        auto t2 = makeShared<BasicTask>();
        auto t3 = makeShared<BasicTask>();
        auto t4 = makeShared<BasicTask>();
        auto t5 = makeShared<BasicTask>();
        auto t6 = makeShared<BasicTask>();
        auto t7 = makeShared<BasicTask>();
        auto t8 = makeShared<BasicTask>();
        auto t9 = makeShared<BasicTask>();

        ConcurrentTask t;

        t.addTask(t1);
        t.addTask(t2);
        t.addTask(t3);
        t.addTask(t4);
        t.addTask(t5);
        t.addTask(t6);
        t.addTask(t7);
        t.addTask(t8);
        t.addTask(t9);

        connect(&t, &Task::finished, &t, [&t, &t1, &t2, &t3, &t4, &t5, &t6, &t7, &t8, &t9] {
            QVERIFY2(t.wasSuccessful(), "Task finished but was not successful when it should have been.");
            QVERIFY(t1->wasSuccessful());
            QVERIFY(t2->wasSuccessful());
            QVERIFY(t3->wasSuccessful());
            QVERIFY(t4->wasSuccessful());
            QVERIFY(t5->wasSuccessful());
            QVERIFY(t6->wasSuccessful());
            QVERIFY(t7->wasSuccessful());
            QVERIFY(t8->wasSuccessful());
            QVERIFY(t9->wasSuccessful());
        });

        t.start();
        QVERIFY2(QTest::qWaitFor([&t]() { return t.isFinished(); }, 1000), "Task didn't finish as it should.");
    }

    void test_basicSequentialRun()
    {
        auto t1 = makeShared<BasicTask>();
        auto t2 = makeShared<BasicTask>();
        auto t3 = makeShared<BasicTask>();

        SequentialTask t;

        t.addTask(t1);
        t.addTask(t2);
        t.addTask(t3);

        connect(&t, &Task::finished, &t, [&t, &t1, &t2, &t3] {
            QVERIFY2(t.wasSuccessful(), "Task finished but was not successful when it should have been.");
            QVERIFY(t1->wasSuccessful());
            QVERIFY(t2->wasSuccessful());
            QVERIFY(t3->wasSuccessful());
        });

        t.start();
        QVERIFY2(QTest::qWaitFor([&t]() { return t.isFinished(); }, 1000), "Task didn't finish as it should.");
    }

    void test_basicMultipleOptionsRun()
    {
        auto t1 = makeShared<BasicTask>();
        auto t2 = makeShared<BasicTask>();
        auto t3 = makeShared<BasicTask>();

        MultipleOptionsTask t;

        t.addTask(t1);
        t.addTask(t2);
        t.addTask(t3);

        connect(&t, &Task::finished, &t, [&t, &t1, &t2, &t3] {
            QVERIFY2(t.wasSuccessful(), "Task finished but was not successful when it should have been.");
            QVERIFY(t1->wasSuccessful());
            QVERIFY(!t2->wasSuccessful());
            QVERIFY(!t3->wasSuccessful());
        });

        t.start();
        QVERIFY2(QTest::qWaitFor([&t]() { return t.isFinished(); }, 1000), "Task didn't finish as it should.");
    }

    void test_stackOverflowInConcurrentTask()
    {
        QEventLoop loop;

        BigConcurrentTaskThread thread;

        connect(&thread, &BigConcurrentTaskThread::finished, &loop, &QEventLoop::quit);

        thread.start();

        loop.exec();

        QVERIFY(!thread.passed_the_deadline);
    }
};

QTEST_GUILESS_MAIN(TaskTest)

#include "Task_test.moc"
