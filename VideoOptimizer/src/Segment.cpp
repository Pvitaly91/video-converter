#include "Segment.h"

#include <iomanip>
#include <sstream>

namespace video_optimizer {

std::int64_t Segment::DurationMilliseconds() const {
    return endMilliseconds - startMilliseconds;
}

std::wstring FormatDuration(std::int64_t milliseconds) {
    if (milliseconds < 0) {
        milliseconds = 0;
    }

    const std::int64_t hours = milliseconds / 3600000;
    milliseconds %= 3600000;
    const std::int64_t minutes = milliseconds / 60000;
    milliseconds %= 60000;
    const std::int64_t seconds = milliseconds / 1000;
    const std::int64_t millis = milliseconds % 1000;

    std::wostringstream stream;
    stream << std::setfill(L'0')
           << std::setw(2) << hours << L":"
           << std::setw(2) << minutes << L":"
           << std::setw(2) << seconds;

    if (millis > 0) {
        stream << L"." << std::setw(3) << millis;
    }

    return stream.str();
}

} // namespace video_optimizer
