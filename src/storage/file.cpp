#include "cupid/storage/file.hpp"

#include <algorithm>
#include <cerrno>
#include <limits>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace cupid::storage {
namespace {

enum class ReserveResult { Created, Occupied, Failed };

struct TemporaryFile {
    std::filesystem::path path;
#ifdef _WIN32
    HANDLE handle{INVALID_HANDLE_VALUE};
#else
    int descriptor{-1};
#endif
};

std::string path_text(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

ReserveResult reserve_temporary(const std::filesystem::path& path, TemporaryFile& file, std::string& error) {
#ifdef _WIN32
    const HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        file.path = path;
        file.handle = handle;
        return ReserveResult::Created;
    }
    const DWORD status = GetLastError();
    if (status == ERROR_FILE_EXISTS || status == ERROR_ALREADY_EXISTS ||
        (status == ERROR_ACCESS_DENIED && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES))
        return ReserveResult::Occupied;
    error = "Unable to reserve temporary file " + path_text(path) + ": " +
            std::error_code(static_cast<int>(status), std::system_category()).message();
    return ReserveResult::Failed;
#else
    const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (descriptor >= 0) {
        file.path = path;
        file.descriptor = descriptor;
        return ReserveResult::Created;
    }
    if (errno == EEXIST)
        return ReserveResult::Occupied;
    error = "Unable to reserve temporary file " + path_text(path) + ": " +
            std::error_code(errno, std::generic_category()).message();
    return ReserveResult::Failed;
#endif
}

bool write_temporary(TemporaryFile& file, std::span<const u8> bytes, std::string& error) {
    bool success = true;
#ifdef _WIN32
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const DWORD amount = static_cast<DWORD>(
            std::min<std::size_t>(remaining, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD written = 0;
        if (WriteFile(file.handle, bytes.data() + offset, amount, &written, nullptr) == 0) {
            const DWORD status = GetLastError();
            error = "Unable to write complete temporary file " + path_text(file.path) + ": " +
                    std::error_code(static_cast<int>(status), std::system_category()).message();
            success = false;
            break;
        }
        if (written != amount) {
            error = "A short write prevented a complete temporary file: " + path_text(file.path);
            success = false;
            break;
        }
        offset += written;
    }
    if (success && FlushFileBuffers(file.handle) == 0) {
        const DWORD status = GetLastError();
        error = "Unable to flush complete temporary file " + path_text(file.path) + ": " +
                std::error_code(static_cast<int>(status), std::system_category()).message();
        success = false;
    }
    if (CloseHandle(file.handle) == 0 && success) {
        const DWORD status = GetLastError();
        error = "Unable to close complete temporary file " + path_text(file.path) + ": " +
                std::error_code(static_cast<int>(status), std::system_category()).message();
        success = false;
    }
    file.handle = INVALID_HANDLE_VALUE;
#else
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t written = ::write(file.descriptor, bytes.data() + offset, bytes.size() - offset);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0) {
            const int status = written == 0 ? EIO : errno;
            error = "Unable to write complete temporary file " + path_text(file.path) + ": " +
                    std::error_code(status, std::generic_category()).message();
            success = false;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    if (success && ::fsync(file.descriptor) != 0) {
        error = "Unable to flush complete temporary file " + path_text(file.path) + ": " +
                std::error_code(errno, std::generic_category()).message();
        success = false;
    }
    if (::close(file.descriptor) != 0 && success) {
        error = "Unable to close complete temporary file " + path_text(file.path) + ": " +
                std::error_code(errno, std::generic_category()).message();
        success = false;
    }
    file.descriptor = -1;
#endif
    return success;
}

} // namespace

bool replace_file(const std::filesystem::path& path, std::span<const u8> bytes, std::string& error) {
    error.clear();
    if (path.empty()) {
        error = "Destination path is empty.";
        return false;
    }

    std::error_code code;
    if (std::filesystem::exists(path, code) && !code && std::filesystem::is_directory(path, code)) {
        error = "Destination path is a directory: " + path_text(path);
        return false;
    }
    auto parent = path.parent_path();
    if (parent.empty())
        parent = ".";
    if (!std::filesystem::is_directory(parent, code) || code) {
        error = "Destination directory is unavailable: " + path_text(parent);
        return false;
    }

    TemporaryFile temporary;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        auto candidate = path;
        candidate += ".tmp." + std::to_string(attempt);
        const auto reserved = reserve_temporary(candidate, temporary, error);
        if (reserved == ReserveResult::Created)
            break;
        if (reserved == ReserveResult::Failed)
            return false;
    }
    if (temporary.path.empty()) {
        error = "Unable to reserve a temporary file beside " + path_text(path);
        return false;
    }

    if (!write_temporary(temporary, bytes, error)) {
        std::filesystem::remove(temporary.path, code);
        return false;
    }

    bool replaced = false;
#ifdef _WIN32
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        replaced = MoveFileExW(temporary.path.c_str(), path.c_str(),
                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        if (replaced)
            break;
        const DWORD status = GetLastError();
        code = std::error_code(static_cast<int>(status), std::system_category());
        if (status != ERROR_ACCESS_DENIED && status != ERROR_SHARING_VIOLATION &&
            status != ERROR_LOCK_VIOLATION)
            break;
        Sleep(1);
    }
#else
    std::filesystem::rename(temporary.path, path, code);
    replaced = !code;
#endif
    if (!replaced) {
        std::error_code cleanup;
        std::filesystem::remove(temporary.path, cleanup);
        error = "Unable to replace destination file " + path_text(path) + ": " + code.message();
        return false;
    }
    return true;
}

} // namespace cupid::storage
