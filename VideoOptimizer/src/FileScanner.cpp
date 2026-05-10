#include "FileScanner.h"

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace video_optimizer {
namespace {

std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool IsOptimizedDirectoryName(const std::filesystem::path& path) {
    return ToLower(path.filename().wstring()) == L"_optimized_mp4";
}

std::wstring Widen(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

} // namespace

FileScanner::FileScanner(std::filesystem::path root, bool recursive)
    : root_(std::move(root)), recursive_(recursive) {
}

ScanResult FileScanner::Scan() const {
    ScanResult result;
    const auto options = std::filesystem::directory_options::skip_permission_denied;

    try {
        if (recursive_) {
            std::error_code ec;
            std::filesystem::recursive_directory_iterator iterator(root_, options, ec);
            const std::filesystem::recursive_directory_iterator end;

            if (ec) {
                result.warnings.push_back(L"Cannot scan directory: " + root_.wstring() + L" (" + Widen(ec.message()) + L")");
                return result;
            }

            for (; iterator != end; iterator.increment(ec)) {
                if (ec) {
                    result.warnings.push_back(L"Scan warning: " + Widen(ec.message()));
                    ec.clear();
                    continue;
                }

                const auto& entry = *iterator;
                if (entry.is_directory(ec)) {
                    if (!ec && ShouldSkipDirectory(entry.path())) {
                        iterator.disable_recursion_pending();
                    }
                    ec.clear();
                    continue;
                }

                if (entry.is_regular_file(ec) && IsSupportedVideoFile(entry.path())) {
                    result.files.push_back(entry.path());
                }
                ec.clear();
            }
        } else {
            std::error_code ec;
            std::filesystem::directory_iterator iterator(root_, options, ec);
            const std::filesystem::directory_iterator end;

            if (ec) {
                result.warnings.push_back(L"Cannot scan directory: " + root_.wstring() + L" (" + Widen(ec.message()) + L")");
                return result;
            }

            for (; iterator != end; iterator.increment(ec)) {
                if (ec) {
                    result.warnings.push_back(L"Scan warning: " + Widen(ec.message()));
                    ec.clear();
                    continue;
                }

                const auto& entry = *iterator;
                if (entry.is_regular_file(ec) && IsSupportedVideoFile(entry.path())) {
                    result.files.push_back(entry.path());
                }
                ec.clear();
            }
        }
    } catch (const std::filesystem::filesystem_error& ex) {
        result.warnings.push_back(L"Scan failed: " + Widen(std::string(ex.what(), std::strlen(ex.what()))));
    }

    std::sort(result.files.begin(), result.files.end());
    return result;
}

bool FileScanner::ShouldSkipDirectory(const std::filesystem::path& directory) const {
    if (IsOptimizedDirectoryName(directory)) {
        return true;
    }

    for (const auto& part : directory) {
        if (IsOptimizedDirectoryName(part)) {
            return true;
        }
    }

    return false;
}

bool FileScanner::IsSupportedVideoFile(const std::filesystem::path& file) const {
    for (const auto& part : file.parent_path()) {
        if (IsOptimizedDirectoryName(part)) {
            return false;
        }
    }

    const std::wstring extension = ToLower(file.extension().wstring());
    return extension == L".mp4"
        || extension == L".mov"
        || extension == L".mkv"
        || extension == L".avi"
        || extension == L".webm"
        || extension == L".m4v";
}

} // namespace video_optimizer
