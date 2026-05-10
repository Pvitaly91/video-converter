#pragma once

#include <cstdint>
#include <string>

namespace video_cutter_gui {

struct TimeValue {
    std::int64_t milliseconds = 0;
    std::wstring normalized;
};

struct SegmentInterval {
    std::int64_t startMilliseconds = 0;
    std::int64_t endMilliseconds = 0;
    std::wstring startText;
    std::wstring endText;

    std::int64_t DurationMilliseconds() const;
};

std::wstring Trim(std::wstring value);
std::wstring FormatTime(std::int64_t milliseconds);
bool ParseTimeText(const std::wstring& text, TimeValue& value, std::wstring& error);
bool ParseSegmentTimes(const std::wstring& startText,
                       const std::wstring& endText,
                       SegmentInterval& segment,
                       std::wstring& error);

} // namespace video_cutter_gui
