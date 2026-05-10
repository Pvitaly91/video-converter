#pragma once

#include "SegmentEditor.h"
#include "VideoDurationProbe.h"

#include <filesystem>

namespace video_cutter_gui {

struct GuiState {
    std::filesystem::path inputFile;
    std::filesystem::path outputFolder;
    std::filesystem::path previewOutputFile;
    std::filesystem::path lastOutputFile;
    VideoInfo videoInfo;
    SegmentEditor segmentEditor;
    bool busy = false;
};

} // namespace video_cutter_gui
