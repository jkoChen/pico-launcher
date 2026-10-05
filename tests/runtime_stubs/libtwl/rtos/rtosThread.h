#pragma once
#include <condition_variable>
#include <thread>
#include "rtosIrq.h"
inline std::function<void()> afterTaskWake;
struct rtos_thread_queue_t
{
    void* unused = nullptr;
    unsigned generation = 0;
    std::condition_variable condition;
};
struct rtos_thread_t
{
    std::thread thread;
    void (*entry)(void*) = nullptr;
    void* argument = nullptr;
};
inline rtos_thread_t* rtos_getCurThread() { return nullptr; }
inline void rtos_queueThread(rtos_thread_t*, rtos_thread_queue_t* queue)
{
    assert(irqDepth == 1);
    auto generation = queue->generation;
    if (beforeTaskWait)
    {
        auto hook = std::move(beforeTaskWait);
        beforeTaskWait = nullptr;
        hook();
    }
    std::unique_lock lock(irqMutex, std::adopt_lock);
    queue->condition.wait(lock, [&] { return queue->generation != generation; });
    lock.release();
}
inline void rtos_wakeupQueue(rtos_thread_queue_t* queue)
{
    assert(irqDepth);
    ++queue->generation;
    queue->condition.notify_all();
    if (afterTaskWake)
    {
        auto hook = std::move(afterTaskWake);
        afterTaskWake = nullptr;
        hook();
    }
}
inline void rtos_createThread(rtos_thread_t* thread, u8, void (*entry)(void*), void* argument, u32*, u32)
{
    thread->entry = entry;
    thread->argument = argument;
}
inline void rtos_wakeupThread(rtos_thread_t* thread)
{
    thread->thread = std::thread(thread->entry, thread->argument);
}
inline void rtos_joinThread(rtos_thread_t* thread) { thread->thread.join(); }
