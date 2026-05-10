#include "Config.h"
#include "VideoOptimizer.h"

#include <Windows.h>

#include <fcntl.h>
#include <io.h>

#include <iostream>

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    _setmode(_fileno(stdout), _O_U8TEXT);
    _setmode(_fileno(stderr), _O_U8TEXT);

    const video_optimizer::ParseResult parseResult = video_optimizer::ParseArguments(argc, argv);
    if (parseResult.helpRequested) {
        video_optimizer::PrintHelp();
        return 0;
    }

    if (!parseResult.ok) {
        std::wcerr << L"Error: " << parseResult.error << L"\n\n";
        video_optimizer::PrintHelp();
        return 1;
    }

    video_optimizer::VideoOptimizer optimizer(parseResult.config);
    return optimizer.Run();
}
