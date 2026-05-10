#include "SegmentParser.h"

#include <algorithm>
#include <cwctype>
#include <sstream>
#include <string>
#include <vector>

namespace video_optimizer {
namespace {

std::wstring Trim(std::wstring value) {
    auto isSpace = [](wchar_t ch) {
        return std::iswspace(ch) != 0;
    };

    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), isSpace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), isSpace).base(), value.end());
    return value;
}

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

bool ParseTimestamp(const std::wstring& text, std::int64_t& milliseconds, std::wstring& normalized) {
    const std::wstring trimmed = Trim(text);
    if (trimmed.empty()) {
        return false;
    }

    const std::vector<std::wstring> parts = Split(trimmed, L':');
    if (parts.empty() || parts.size() > 3) {
        return false;
    }

    std::int64_t hours = 0;
    std::int64_t minutes = 0;
    std::int64_t seconds = 0;
    std::int64_t millis = 0;

    if (parts.size() == 1) {
        if (!ParseSecondsPart(parts[0], seconds, millis)) {
            return false;
        }
    } else if (parts.size() == 2) {
        if (!ParseWholeNumber(parts[0], minutes) || !ParseSecondsPart(parts[1], seconds, millis)) {
            return false;
        }
        if (seconds >= 60) {
            return false;
        }
    } else {
        if (!ParseWholeNumber(parts[0], hours)
            || !ParseWholeNumber(parts[1], minutes)
            || !ParseSecondsPart(parts[2], seconds, millis)) {
            return false;
        }
        if (minutes >= 60 || seconds >= 60) {
            return false;
        }
    }

    milliseconds = (((hours * 60) + minutes) * 60 + seconds) * 1000 + millis;
    normalized = FormatDuration(milliseconds);
    return true;
}

} // namespace

bool ParseSegmentList(const std::wstring& text, std::vector<Segment>& segments, std::wstring& error) {
    segments.clear();

    const std::vector<std::wstring> segmentTexts = Split(text, L',');
    if (segmentTexts.empty()) {
        error = L"Segments list is empty.";
        return false;
    }

    for (size_t index = 0; index < segmentTexts.size(); ++index) {
        const std::wstring segmentText = Trim(segmentTexts[index]);
        if (segmentText.empty()) {
            error = L"Empty segments are not allowed.";
            return false;
        }

        const size_t delimiter = segmentText.find(L'-');
        if (delimiter == std::wstring::npos || delimiter != segmentText.rfind(L'-')) {
            error = L"Invalid segment format: " + segmentText;
            return false;
        }

        const std::wstring startText = Trim(segmentText.substr(0, delimiter));
        const std::wstring endText = Trim(segmentText.substr(delimiter + 1));
        if (startText.empty() || endText.empty()) {
            error = L"Segment start and end must not be empty.";
            return false;
        }

        Segment segment;
        if (!ParseTimestamp(startText, segment.startMilliseconds, segment.start)) {
            error = L"Invalid segment start time: " + startText;
            return false;
        }

        if (!ParseTimestamp(endText, segment.endMilliseconds, segment.end)) {
            error = L"Invalid segment end time: " + endText;
            return false;
        }

        if (segment.startMilliseconds >= segment.endMilliseconds) {
            error = L"Segment START must be less than END: " + segmentText;
            return false;
        }

        segments.push_back(segment);
    }

    return true;
}

} // namespace video_optimizer
