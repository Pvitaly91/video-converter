#pragma once

#include "TimeParser.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace video_cutter_gui {

class SegmentEditor {
public:
    bool Add(const std::wstring& startText,
             const std::wstring& endText,
             std::optional<std::int64_t> knownDurationMilliseconds,
             std::wstring& error);

    bool Remove(size_t index);
    bool MoveUp(size_t index);
    bool MoveDown(size_t index);
    void Clear();

    const std::vector<SegmentInterval>& Segments() const;
    bool Empty() const;
    size_t Size() const;

private:
    std::vector<SegmentInterval> segments_;
};

} // namespace video_cutter_gui
