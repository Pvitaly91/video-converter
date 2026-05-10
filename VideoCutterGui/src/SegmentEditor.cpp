#include "SegmentEditor.h"

#include <algorithm>

namespace video_cutter_gui {

bool SegmentEditor::Add(const std::wstring& startText,
                        const std::wstring& endText,
                        std::optional<std::int64_t> knownDurationMilliseconds,
                        std::wstring& error) {
    SegmentInterval segment;
    if (!ParseSegmentTimes(startText, endText, segment, error)) {
        return false;
    }

    if (knownDurationMilliseconds.has_value()
        && segment.endMilliseconds > knownDurationMilliseconds.value() + 1) {
        error = L"End time is greater than the known video duration.";
        return false;
    }

    segments_.push_back(segment);
    return true;
}

bool SegmentEditor::Remove(size_t index) {
    if (index >= segments_.size()) {
        return false;
    }

    segments_.erase(segments_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool SegmentEditor::MoveUp(size_t index) {
    if (index == 0 || index >= segments_.size()) {
        return false;
    }

    std::swap(segments_[index - 1], segments_[index]);
    return true;
}

bool SegmentEditor::MoveDown(size_t index) {
    if (index + 1 >= segments_.size()) {
        return false;
    }

    std::swap(segments_[index], segments_[index + 1]);
    return true;
}

void SegmentEditor::Clear() {
    segments_.clear();
}

const std::vector<SegmentInterval>& SegmentEditor::Segments() const {
    return segments_;
}

bool SegmentEditor::Empty() const {
    return segments_.empty();
}

size_t SegmentEditor::Size() const {
    return segments_.size();
}

} // namespace video_cutter_gui
