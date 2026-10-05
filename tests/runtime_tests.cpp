#include "common.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "core/task/TaskQueue.h"
#include "browser.h"
#include "romBrowser/DisplayMode/BannerListFileRecyclerAdapter.h"

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "CHECK FAILED: %s (line %d)\n", #condition, __LINE__); \
    std::abort(); } } while (false)

extern "C" void shared_ptr_increase_ref_count(vu32& count)
{
    auto irq = rtos_disableIrqs();
    count = count + 1;
    rtos_restoreIrqs(irq);
}

using TestQueue = TaskQueue<32, sizeof(TaskBase) + 32>;

void WaitIdle(TestQueue& queue)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (true)
    {
        auto irq = rtos_disableIrqs();
        bool idle = queue.IsIdle();
        rtos_restoreIrqs(irq);
        if (idle)
            return;
        CHECK(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
}

struct TrackedView : EnableSharedFromThis<TrackedView>
{
    static inline int destroyed = 0;
    ~TrackedView() { ++destroyed; }
};

void TestWeakReset()
{
    for (int i = 0; i < 1000; ++i)
    {
        auto view = SharedPtr<TrackedView>::MakeShared();
        auto weak = view->WeakFromThis();
        auto second = weak;
        CHECK(weak.Lock().GetPointer() == view.GetPointer());
        weak.Reset();
        CHECK(!weak.Lock());
        weak.Reset();
        second = second;
        second = std::move(second);
        CHECK(second.Lock().GetPointer() == view.GetPointer());
        view.Reset();
        CHECK(!second.Lock());
        second.Reset();
        CHECK(TrackedView::destroyed == i + 1);

        auto value = SharedPtr<int>::MakeShared(42);
        WeakPtr<int> external(value);
        external.Reset(); // Last weak reference must not delete a live control block.
        CHECK(*value == 42);
        WeakPtr<int> empty;
        auto moved = std::move(empty);
        CHECK(!moved.Lock());
    }
}

void TestPendingQueue()
{
    TestQueue queue;
    int calls = 0;
    for (int i = 0; i < 200; ++i)
    {
        auto pending = queue.Enqueue([&] (const vu8&) {
            ++calls;
            return TaskResult<void>::Completed();
        });
        pending.CancelTask();
        CHECK(queue.IsIdle()); // Single canceled node must be unlinked, not left dangling.
    }
    auto work = queue.Enqueue([&] (const vu8&) {
        ++calls;
        return TaskResult<void>::Completed();
    });
    queue.StartThread(1, nullptr, 0);
    WaitIdle(queue);
    CHECK(calls == 1);
    work.Dispose();
    queue.StopThread();
}

void TestCompletionOwnership()
{
    struct Capture
    {
        int* destroyed;
        explicit Capture(int* destroyed) : destroyed(destroyed) { }
        ~Capture() { CHECK(irqDepth == 0); ++*destroyed; }
    };
    TestQueue queue;
    int destroyed = 0;
    auto capture = SharedPtr<Capture>::MakeShared(&destroyed);
    auto work = queue.Enqueue([capture] (const vu8&) { return TaskResult<void>::Completed(); });
    capture.Reset();
    // Simulate the UI returning ownership as soon as completion wakes it, before
    // the worker has returned from Execute and inspected its final ownership flag.
    afterTaskWake = [&] {
        work.Dispose();
        CHECK(destroyed == 0);
    };
    queue.StartThread(1, nullptr, 0);
    WaitIdle(queue);
    queue.StopThread();
    CHECK(destroyed == 1);
}

struct Gate
{
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool released = false;
    void Pause()
    {
        std::unique_lock lock(mutex);
        entered = true;
        condition.notify_all();
        condition.wait(lock, [&] { return released; });
    }
    void WaitEntered()
    {
        std::unique_lock lock(mutex);
        CHECK(condition.wait_for(lock, std::chrono::seconds(3), [&] { return entered; }));
    }
    void Release()
    {
        std::lock_guard lock(mutex);
        released = true;
        condition.notify_all();
    }
};

void TestRowReuse()
{
    for (int i = 0; i < 50; ++i)
    {
        TestQueue queue;
        FileInfoManager files;
        IRomBrowserController controller(&files);
        IRomBrowserViewFactory factory;
        BannerListFileRecyclerAdapter adapter(&controller, &files, &queue, nullptr, &factory, nullptr);
        auto row = SharedPtr<BannerListItemView>(adapter.CreateView());
        Gate gate;
        row->beforeFileNameWrite = [&] { gate.Pause(); };
        adapter.BindView(row, 0);
        queue.StartThread(1, nullptr, 0);
        gate.WaitEntered(); // Old callback is paused just before writing the filename.
        beforeTaskWait = [&] { gate.Release(); };
        adapter.ReleaseView(row, 0);
        gate.Release(); // Also lets the unfixed non-waiting baseline complete/fail.
        WaitIdle(queue);
        beforeTaskWait = nullptr;
        CHECK(row->text.empty());
        CHECK(!row->icon);

        row->beforeFileNameWrite = nullptr;
        adapter.BindView(row, 1);
        WaitIdle(queue);
        CHECK(row->text == files.GetItem(1).GetFileName());
        CHECK(row->icon && row->icon->file == 1);
        row->GetViewModel().Activate();
        CHECK(controller.launched == row->text);
        row->GetViewModel().DisposeQueueTaskWhenComplete();
        row->GetViewModel().DisposeQueueTaskWhenComplete(); // Empty handles are safe on later frames.
        adapter.ReleaseView(row, 1);
        queue.StopThread();
    }
}

void TestImmediateIdentity()
{
    TestQueue queue; // Leave IO pending, like a slow SD card during paging.
    FileInfoManager files;
    IRomBrowserController controller(&files);
    IRomBrowserViewFactory factory;
    BannerListFileRecyclerAdapter adapter(&controller, &files, &queue, nullptr, &factory, nullptr);
    auto row = SharedPtr<BannerListItemView>(adapter.CreateView());
    adapter.BindView(row, 1);
    row->GetViewModel().Activate();
    CHECK(controller.launched == files.GetItem(1).GetFileName());
    adapter.ReleaseView(row, 1);
    CHECK(queue.IsIdle());
}

int main(int argc, char** argv)
{
    CHECK(argc == 2);
    std::string name = argv[1];
    if (name == "weak-reset") TestWeakReset();
    else if (name == "pending-queue") TestPendingQueue();
    else if (name == "completion-ownership") TestCompletionOwnership();
    else if (name == "row-reuse") TestRowReuse();
    else if (name == "immediate-identity") TestImmediateIdentity();
    else CHECK(false);
    std::printf("PASS: %s\n", argv[1]);
}
