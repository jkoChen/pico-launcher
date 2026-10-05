#include "common.h"
#include "core/task/TaskQueue.h"
#include "FileInfoManager.h"
#include "FileRecyclerAdapter.h"
#include "viewModels/RomBrowserItemViewModel.h"

u32 FileRecyclerAdapter::GetItemCount() const
{
    return _fileInfoManager->GetItemCount();
}

void FileRecyclerAdapter::BindView(SharedPtr<View> view, int index) const
{
    LOG_DEBUG("Binding %d\n", index);
    auto& viewModel = GetItemViewModel(view);
    viewModel.CancelQueueTask();
    // Input must target this row's new file even while its icon is still loading.
    viewModel.SetIndex(index);
    auto queueTask = _taskQueue->Enqueue([=, this] (const vu8& cancelRequested)
    {
        if (cancelRequested)
        {
            LOG_DEBUG("Task to load %d was canceled\n", index);
            return TaskResult<void>::Canceled();
        }

        LOG_DEBUG("Started task to load %d\n", index);
        _fileInfoManager->LoadFileInfo(index);
        if (cancelRequested)
        {
            _fileInfoManager->ReleaseFileInfo(index);
            return TaskResult<void>::Canceled();
        }
        return TaskResult<void>::Completed();
    });
    viewModel.SetQueueTask(std::move(queueTask), [=, this]
    {
        // Publish the title and icon together on the UI thread, after IO has
        // completed. ReleaseView discards this callback before row reuse.
        const vu8 cancelRequested = false;
        BindView(view, index, _fileInfoManager->GetInternalFileInfo(index), cancelRequested);
    });
}
