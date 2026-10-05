#pragma once
#include <functional>
#include <string>
#include <vector>
#include "core/EnableSharedFromThis.h"
#include "romBrowser/viewModels/RomBrowserItemViewModel.h"

class View : public EnableSharedFromThis<View>
{
public:
    virtual ~View() = default;
};
class VramContext { };
class VBlankTextureLoader { };
class IThemeFileIconFactory { };
class MockIcon
{
public:
    int file;
    explicit MockIcon(int file) : file(file) { }
    void SetAnimFrame(u32) { }
};
enum class FileTypeClassification { Folder, Nds };
class FileType
{
public:
    const char* GetShortName() const { return "nds"; }
    FileTypeClassification GetClassification() const { return FileTypeClassification::Nds; }
    std::unique_ptr<MockIcon> CreateFileIcon(const char*, const IThemeFileIconFactory*) const
    { return std::make_unique<MockIcon>(-1); }
};
class NdsFileType
{
public:
    inline static FileType sInstance;
};
class FileInfo
{
    std::string _name;
public:
    explicit FileInfo(const char* name) : _name(name) { }
    const char* GetFileName() const { return _name.c_str(); }
    const FileType* GetFileType() const { return &NdsFileType::sInstance; }
};
class InternalFileInfo
{
    int _file;
public:
    explicit InternalFileInfo(int file) : _file(file) { }
    const char16_t* GetGameTitle() const { return nullptr; }
    std::unique_ptr<MockIcon> CreateGameIcon() const { return std::make_unique<MockIcon>(_file); }
};
class FileInfoManager
{
    std::array<FileInfo, 2> _items { FileInfo("逆转裁判4.nds"), FileInfo("节奏天国.nds") };
    std::array<std::unique_ptr<InternalFileInfo>, 2> _info;
public:
    u32 GetItemCount() const { return _items.size(); }
    const FileInfo& GetItem(int index) const { return _items.at(index); }
    void LoadFileInfo(int index) { _info.at(index) = std::make_unique<InternalFileInfo>(index); }
    const InternalFileInfo* GetInternalFileInfo(int index) const { return _info.at(index).get(); }
    void ReleaseFileInfo(int index) { _info.at(index).reset(); }
};
class RomBrowserViewModel
{
    FileInfoManager* _files;
public:
    explicit RomBrowserViewModel(FileInfoManager* files) : _files(files) { }
    FileInfoManager& GetFileInfoManager() const { return *_files; }
};
class IRomBrowserController
{
    RomBrowserViewModel _model;
public:
    struct Settings { bool ndsFileNameAsTitle = true; } settings;
    std::string launched;
    explicit IRomBrowserController(FileInfoManager* files) : _model(files) { }
    const Settings& GetRomBrowserDisplaySettings() const { return settings; }
    RomBrowserViewModel* GetRomBrowserViewModel() { return &_model; }
    void LaunchFile(const FileInfo& file) { launched = file.GetFileName(); }
    void NavigateToPath(const char*) { }
    void ShowGameInfo(const FileInfo&) { }
};
class BannerListItemView : public View
{
    std::unique_ptr<RomBrowserItemViewModel> _model;
public:
    using VramToken = int;
    std::string text;
    std::unique_ptr<MockIcon> icon;
    std::function<void()> beforeFileNameWrite;
    explicit BannerListItemView(std::unique_ptr<RomBrowserItemViewModel> model) : _model(std::move(model)) { }
    RomBrowserItemViewModel& GetViewModel() { return *_model; }
    void SetGraphics(VramToken) { }
    void SetGameTitle(const char16_t*) { text.clear(); }
    void SetFileName(const char* name, bool useAsTitle)
    {
        if (beforeFileNameWrite)
            beforeFileNameWrite();
        if (useAsTitle)
            text = name;
    }
    void SetIcon(std::unique_ptr<MockIcon> value) { icon = std::move(value); }
    void UploadIconGraphics() { }
};
class IRomBrowserViewFactory
{
public:
    SharedPtr<View> CreateBannerListItemView(std::unique_ptr<RomBrowserItemViewModel> model, VBlankTextureLoader*) const
    { return SharedPtr<BannerListItemView>::MakeShared(std::move(model)); }
    int UploadBannerListItemViewGraphics(const VramContext&) const { return 0; }
};
