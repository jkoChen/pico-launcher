#pragma once
#include <functional>
#include "core/task/TaskQueue.h"

class IRomBrowserController;

class RomBrowserItemViewModel
{
public:
    explicit RomBrowserItemViewModel(IRomBrowserController* romBrowserController)
        : _romBrowserController(romBrowserController) { }

    void Activate();
    void ShowGameInfo();

    void SetIndex(int index)
    {
        _index = index;
    }

    void SetQueueTask(QueueTask<void> queueTask, std::function<void()> onCompleted)
    {
        _queueTask = std::move(queueTask);
        _onCompleted = std::move(onCompleted);
    }

    void CancelQueueTask()
    {
        // ReleaseView must finish the old callback before clearing/reusing the row.
        _queueTask.CancelTaskAndWait();
        _onCompleted = nullptr;
    }

    void DisposeQueueTaskWhenComplete()
    {
        if (_queueTask.IsValid() && _queueTask.GetTask().IsCompleted())
        {
            bool completed = _queueTask.GetTask().IsCompletedSuccessfully();
            _queueTask.Dispose();
            auto onCompleted = std::move(_onCompleted);
            _onCompleted = nullptr;
            // Called by the row's Update on the UI thread. Never render labels
            // or upload graphics concurrently with that same row's Update.
            if (completed && onCompleted)
                onCompleted();
        }
    }

private:
    int _index = -1;
    QueueTask<void> _queueTask;
    std::function<void()> _onCompleted;

    IRomBrowserController* _romBrowserController;
};
