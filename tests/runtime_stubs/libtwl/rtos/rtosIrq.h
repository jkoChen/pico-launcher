#pragma once
#include <cassert>
#include <functional>
#include <mutex>
#include "common.h"

// A host mutex models the single-core IRQ critical sections. This is not an
// emulator: SD/GPU/RTOS timing still needs verification on a real console.
inline std::mutex irqMutex;
inline thread_local unsigned irqDepth = 0;
inline std::function<void()> beforeTaskWait;
inline u32 rtos_disableIrqs()
{
    u32 previous = irqDepth;
    if (irqDepth++ == 0)
        irqMutex.lock();
    return previous;
}
inline void rtos_restoreIrqs(u32 previous)
{
    assert(irqDepth == previous + 1);
    irqDepth = previous;
    if (!irqDepth)
        irqMutex.unlock();
}
