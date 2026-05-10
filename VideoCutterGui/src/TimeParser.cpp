#include "TimeParser.h"

#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <vector>

namespace video_cutter_gui {
namespace {

std::vector<std::wstring> Split(const std::wstring& text, wchar_t delimiter) {
    std::vector<std::wstring> parts;
    std::wstring current;
    std::wstringstream stream(text);

    while (std::getline(stream, current, delimiter)) {
        parts.push_back(current);
    }

    if (!text.empty() && text.back() == delimiter) {
        parts.push_back(L"");
    }

    return parts;
}

bool IsDigits(const std::wstring& value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](wchar_t ch) {
        return std::iswdigit(ch) != 0;
    });
}

bool ParseWholeNumber(const std::wstring& text, std::int64_t& value) {
    if (!IsDigits(text)) {
        return false;
    }

    try {
        size_t parsed = 0;
        value = std::stoll(text, &parsed, 10);
        return parsed == text.size();
    } catch (...) {
        return false;
    }
}

bool ParseSecondsPart(const std::wstring& text, std::int64_t& seconds, std::int64_t& milliseconds) {
    const size_t dot = text.find(L'.');
    if (dot == std::wstring::npos) {
        milliseconds = 0;
        return ParseWholeNumber(text, seconds);
    }

    const std::wstring whole = text.substr(0, dot);
    std::wstring fraction = text.substr(dot + 1);

    if (!ParseWholeNumber(whole, seconds) || fraction.empty() || fraction.size() > 3 || !IsDigits(fraction)) {
        return false;
    }

    while (fraction.size() < 3) {
        fraction.push_back(L'0');
    }

    return ParseWholeNumber(fraction, milliseconds);
}

} // namespace

std::int64_t SegmentInterval::DurationMilliseconds() const {
    return endMilliseconds - startMilliseconds;
}

std::wstring Trim(std::wstring value) {
    auto isSpace = [](wchar_t ch) {
        return std::iswspace(ch) != 0;
    };

    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), isSpace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), isSpace).base(), value.end());
    return value;
}

std::wstring FormatTime(std::int64_t milliseconds) {
    if (milliseconds < 0) {
        milliseconds = 0;
    }

    const std::int64_t totalSeconds = milliseconds / 1000;
    const std::int64_t millis = milliseconds % 1000;
    const std::int64_t seconds = totalSeconds % 60;
    const std::int64_t minutes = (totalSeconds / 60) % 60;
    const std::int64_t hours = totalSeconds / 3600;

    std::wostringstream stream;
    stream << std::setfill(L'0') << std::setw(2) << hours << L":"
           << std::setw(2) << minutes << L":"
           << std::setw(2) << seconds;

    if (millis > 0) {
        stream << L"." << std::setw(3) << millis;
    }

    return stream.str();
}

bool ParseTimeText(const std::wstring& text, TimeValue& value, std::wstring& error) {
    const std::wstring trimmed = Trim(text);
    if (trimmed.empty()) {
        error = L"Time value is empty.";
        return false;
    }

    const std::vector<std::wstring> parts = Split(trimmed, L':');
    if (parts.empty() || parts.size() > 3) {
        error = L"Invalid time format: " + trimmed;
        return false;
    }

    std::int64_t hours = 0;
    std::int64_t minutes = 0;
    std::int64_t seconds = 0;
    std::int64_t millis = 0;

    if (parts.size() == 1) {
        if (!ParseSecondsPart(parts[0], seconds, millis)) {
            error = L"Invalid seconds value: " + trimmed;
            return false;
        }
    } else if (parts.size() == 2) {
        if (!ParseWholeNumber(parts[0], minutes) || !ParseSecondsPart(parts[1], seconds, millis)) {
            error = L"Invalid MM:SS value: " + trimmed;
            return false;
        }
        if (seconds >= 60) {
            error = L"Seconds must be less than 60 in MM:SS format: " + trimmed;
            return false;
        }
    } else {
        if (!ParseWholeNumber(parts[0], hours)
            || !ParseWholeNumber(parts[1], minutes)
            || !ParseSecondsPart(parts[2], seconds, millis)) {
            error = L"Invalid HH:MM:SS value: " + trimmed;
            return false;
        }
        if (minutes >= 60 || seconds >= 60) {
            error = L"Minutes and seconds must be less than 60 in HH:MM:SS format: " + trimmed;
            return false;
        }
    }

    value.milliseconds = (((hours * 60) + minutes) * 60 + seconds) * 1000 + millis;
    value.normalized = FormatTime(value.milliseconds);
    return true;
}

bool ParseSegmentTimes(const std::wstring& startText,
                       const std::wstring& endText,
                       SegmentInterval& segment,
                       std::wstring& error) {
    TimeValue start;
    TimeValue end;

    if (!ParseTimeText(startText, start, error)) {
        error = L"Invalid start time. " + error;
        return false;
    }

    if (!ParseTimeText(endText, end, error)) {
        error = L"Invalid end time. " + error;
        return false;
    }

    if (start.milliseconds >= end.milliseconds) {
        error = L"Start must be less than End.";
        return false;
    }

    segment.startMilliseconds = start.milliseconds;
    segment.endMilliseconds = end.milliseconds;
    segment.startText = start.normalized;
    segment.endText = end.normalized;
    return true;
}

} // namespace video_cutter_gui
