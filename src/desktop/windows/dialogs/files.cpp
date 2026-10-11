#include "desktop/windows/dialogs/paths.hpp"
#include <shobjidl.h>
#include <stdexcept>

namespace cupid::desktop {
namespace {

template <class Interface> struct ComPointer {
  Interface *value = nullptr;
  ~ComPointer() {
    if (value)
      value->Release();
  }
};

void require(HRESULT result) {
  if (FAILED(result))
    throw std::runtime_error("Could not open the file or folder picker.");
}

} // namespace

std::optional<std::filesystem::path> select_file(HWND owner, const wchar_t *title,
                                                 FileSelection kind,
                                                 const std::filesystem::path &directory,
                                                 const std::filesystem::path &current) {
  const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
    require(initialized);
  struct Apartment {
    bool acquired;
    ~Apartment() {
      if (acquired)
        CoUninitialize();
    }
  } apartment{SUCCEEDED(initialized)};
  ComPointer<IFileDialog> dialog;
  const bool save = kind == FileSelection::Screenshot;
  require(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr,
                           CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog.value)));
  DWORD options = 0;
  require(dialog.value->GetOptions(&options));
  options |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | FOS_PATHMUSTEXIST;
  options |= kind == FileSelection::Folder ? FOS_PICKFOLDERS
                                           : (save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST);
  require(dialog.value->SetOptions(options));
  require(dialog.value->SetTitle(title));
  const COMDLG_FILTERSPEC filter[]{{L"Nintendo 64 cartridges", L"*.z64;*.n64;*.v64"},
                                   {L"PIF firmware", L"*.bin;*.rom"},
                                   {L"64DD IPL firmware", L"*.z64;*.n64;*.v64;*.bin;*.rom"},
                                   {L"64DD disk images", L"*.ndd;*.d64"},
                                   {L"Folders", L"*.*"},
                                   {L"Bitmap image", L"*.bmp"},
                                   {L"Game Boy and Game Boy Color cartridges", L"*.gb;*.gbc"},
                                   {L"HLSL display pixel shaders", L"*.hlsl"}};
  const COMDLG_FILTERSPEC filters[]{filter[static_cast<unsigned>(kind)], {L"All files", L"*.*"}};
  if (kind != FileSelection::Folder) {
    require(dialog.value->SetFileTypes(save ? 1 : 2, filters));
    if (save)
      require(dialog.value->SetDefaultExtension(L"bmp"));
  }
  if (!directory.empty()) {
    ComPointer<IShellItem> folder;
    require(SHCreateItemFromParsingName(directory.c_str(), nullptr, IID_PPV_ARGS(&folder.value)));
    require(dialog.value->SetFolder(folder.value));
  }
  if (!current.empty() && kind != FileSelection::Folder)
    require(dialog.value->SetFileName(current.filename().c_str()));
  const auto result = dialog.value->Show(owner);
  if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
    return std::nullopt;
  require(result);
  ComPointer<IShellItem> item;
  require(dialog.value->GetResult(&item.value));
  PWSTR name = nullptr;
  require(item.value->GetDisplayName(SIGDN_FILESYSPATH, &name));
  struct Name {
    PWSTR text;
    ~Name() {
      CoTaskMemFree(text);
    }
  } selected{name};
  return std::filesystem::path(name);
}

} // namespace cupid::desktop
