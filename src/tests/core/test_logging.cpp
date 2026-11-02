#include "core/util/logging.h"
#include "horizon/core/logging.h"

#include <fstream>
#include <iterator>
#include <string>

int main() {
    const auto path = std::filesystem::current_path() / "test-core-shared-logging.log";
    horizon::core::LoggingOptions options;
    options.console = false;
    options.file_path = path;
    options.level = horizon::core::LogLevel::Debug;
    horizon::core::initialize_logging(options);

    OC_DEBUG("renderer debug ", 7);
    OC_INFO("renderer literal {} ", 42);
    CFW_LOG_INFO("engine formatted {}", 9);
    horizon::core::set_log_level(horizon::core::LogLevel::Warning);
    OC_INFO("suppressed renderer info");
    OC_WARNING("renderer warning");
    ocarina::log_level_error();
    CFW_LOG_WARNING("suppressed engine warning");
    ocarina::log_level_debug();
    CFW_LOG_DEBUG("engine debug restored");
    horizon::core::log_flush();

    std::ifstream file{path};
    const std::string output{std::istreambuf_iterator<char>{file}, {}};
    for (const auto *message : {"renderer debug 7", "renderer literal {} 42",
                                "engine formatted 9", "renderer warning", "engine debug restored"}) {
        if (output.find(message) == std::string::npos) {
            std::cerr << "Shared logging lost message: " << message << '\n';
            return 1;
        }
    }
    if (output.find("suppressed renderer info") != std::string::npos ||
        output.find("suppressed engine warning") != std::string::npos) {
        std::cerr << "Engine and renderer must share log-level filtering\n";
        return 1;
    }
    return 0;
}
