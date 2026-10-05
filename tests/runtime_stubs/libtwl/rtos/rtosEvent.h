#pragma once
#include <condition_variable>
#include "rtosIrq.h"
struct rtos_event_t
{
    bool signaled = false;
    std::condition_variable condition;
};
inline void rtos_createEvent(rtos_event_t*) { }
inline void rtos_signalEvent(rtos_event_t* event)
{
    auto irq = rtos_disableIrqs();
    event->signaled = true;
    event->condition.notify_one();
    rtos_restoreIrqs(irq);
}
inline void rtos_waitEvent(rtos_event_t* event, bool, bool clear)
{
    auto irq = rtos_disableIrqs();
    std::unique_lock lock(irqMutex, std::adopt_lock);
    event->condition.wait(lock, [&] { return event->signaled; });
    if (clear)
        event->signaled = false;
    lock.release();
    rtos_restoreIrqs(irq);
}
