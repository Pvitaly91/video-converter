#include "ConcatListWriter.h"

#include <Windows.h>

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace video_optimizer {
namespace {

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }

    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring EscapeConcatPath(const std::filesystem::path& path) {
    std::wstring escaped = std::filesystem::absolute(path).generic_wstring();
    size_t position = 0;

    while ((position = escaped.find(L'\'', position)) != std::wstring::npos) {
        escaped.replace(position, 1, L"'\\''");
        position += 4;
    }

    return escaped;
}

std::wstring Widen(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

} // namespace

bool ConcatListWriter::Write(const std::filesystem::path& listFile,
                             const std::vector<std::filesystem::path>& partFiles,
                             std::wstring& error) {
    std::ofstream output(listFile, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = L"Cannot create concat list: " + listFile.wstring();
        return false;
    }

    for (const auto& part : partFiles) {
        const std::wstring line = L"file '" + EscapeConcatPath(part) + L"'\n";
        const std::string utf8 = WideToUtf8(line);
        if (utf8.empty()) {
            error = L"Cannot encode concat list entry: " + part.wstring();
            return false;
        }
        output.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    }

    if (!output.good()) {
        error = L"Failed to write concat list: " + Widen(std::generic_category().message(errno));
        return false;
    }

    return true;
}

} // namespace video_optimizer
