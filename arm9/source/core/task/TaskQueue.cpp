#include "common.h"
#include "TaskQueue.h"

void TaskQueueBase::ThreadMain()
{
    while (true)
    {
        _idle = false;
        while (true)
        {
            u32 irqs = rtos_disableIrqs();
            auto task = _taskList.GetHead();
            if (!task)
            {
                rtos_restoreIrqs(irqs);
                break;
            }
            _taskList.Remove(task);
            _executingTask = task;
            task->Execute(irqs);
            // Completion can wake the UI, which may dispose its handle before
            // Execute returns. Keep worker ownership until this last access.
            irqs = rtos_disableIrqs();
            bool destroyWhenComplete = task->GetDestroyWhenComplete();
            _executingTask = nullptr;
            rtos_restoreIrqs(irqs);
            if (destroyWhenComplete)
            {
                // this will destroy the task
                ReturnOwnership(task);
            }
        }
        if (_endThreadWhenDone)
            return;
        _idle = true;
        rtos_waitEvent(&_event, false, true);
    }
}

void QueueTaskBase::Dispose()
{
    if (_task)
    {
        TaskBase* task = _task;
        _task = nullptr;
        _taskQueue->ReturnOwnership(task);
        _taskQueue = nullptr;
    }
}
