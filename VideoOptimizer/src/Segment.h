#pragma once

#include <cstdint>
#include <string>

namespace video_optimizer {

struct Segment {
    std::wstring start;
    std::wstring end;
    std::int64_t startMilliseconds = 0;
    std::int64_t endMilliseconds = 0;

    std::int64_t DurationMilliseconds() const;
};

std::wstring FormatDuration(std::int64_t milliseconds);

} // namespace video_optimizer
