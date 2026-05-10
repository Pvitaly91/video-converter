#pragma once

#include "Segment.h"

#include <string>
#include <vector>

namespace video_optimizer {

bool ParseSegmentList(const std::wstring& text, std::vector<Segment>& segments, std::wstring& error);

} // namespace video_optimizer
