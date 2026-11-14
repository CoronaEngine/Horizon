#include "example_baseline/example_baseline.h"
#include "example_edsl/example_edsl.h"
#include "example_glsl/example_glsl.h"
#include "example_ibl/example_ibl.h"
#include "example_drawstress/example_drawstress.h"
#include "example_edsl_ibl/example_edsl_ibl.h"
#include "example_sponza/example_sponza.h"
#include "example_shadowmaps/example_shadowmaps.h"
#include "example_edsl_shadowmaps/example_edsl_shadowmaps.h"
#include "example_shadowvolumes/example_shadowvolumes.h"
#include "example_edsl_shadowvolumes/example_edsl_shadowvolumes.h"
#include "example_sky/example_sky.h"
#include "example_ssr/example_ssr.h"
#include "example_edsl_ssr/example_edsl_ssr.h"
#include "example_edsl_sky/example_edsl_sky.h"
#include "example_disney_pbr/example_disney_pbr.h"
#include "example_edsl_disney_pbr/example_edsl_disney_pbr.h"
#include "example_rsm/example_rsm.h"
#include "example_edsl_rsm/example_edsl_rsm.h"
#include "example_edsl_sponza/example_edsl_sponza.h"
#include "example_edsl_drawstress/example_edsl_drawstress.h"
#include <array>
#include <exception>

#include "horizon/core/logging.h"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string_view>

int main(int argc, char **argv)
{
    // Application policy: keep the timestamped file and crash flushing enabled.
    const auto now = std::time(nullptr);
    std::tm local_time {};
#ifdef _WIN32
    localtime_s(&local_time, &now);
#else
    localtime_r(&now, &local_time);
#endif
    std::ostringstream log_filename;
    log_filename << std::put_time(&local_time, "%Y-%m-%d_%H-%M-%S") << "_corona.log";
    horizon::core::LoggingOptions logging;
    logging.file_path = std::filesystem::path { "logs" } / log_filename.str();
    logging.install_signal_handlers = true;
    logging.configure_utf8_console = true;
#ifdef CORONA_LOG_LEVEL
    logging.level = static_cast<horizon::core::LogLevel>(CORONA_LOG_LEVEL);
#endif
    horizon::core::initialize_logging(logging);

    const std::string_view mode = argc > 1 ? std::string_view(argv[1]) : std::string_view("sponza");

    struct Example { std::string_view name; void (*run)(); };
    static constexpr Example examples[] = {
        {"baseline", run_example_baseline},
        {"disney_pbr", [] { run_example_disney_pbr(); }},
        {"drawstress", [] { run_example_drawstress(); }},
        {"edsl", run_example_edsl},
        {"edsl_disney_pbr", run_example_edsl_disney_pbr},
        {"edsl_drawstress", run_example_edsl_drawstress},
        {"edsl_ibl", run_example_edsl_ibl},
        {"edsl_rsm", run_example_edsl_rsm},
        {"edsl_shadowmaps", run_example_edsl_shadowmaps},
        {"edsl_shadowvolumes", run_example_edsl_shadowvolumes},
        {"edsl_sky", run_example_edsl_sky},
        {"edsl_sponza", run_example_edsl_sponza},
        {"edsl_ssr", run_example_edsl_ssr},
        {"glsl", run_example_glsl},
        {"ibl", [] { run_example_ibl(); }},
        {"rsm", run_example_rsm},
        {"shadowmaps", run_example_shadowmaps},
        {"shadowvolumes", run_example_shadowvolumes},
        {"sky", run_example_sky},
        {"sponza", run_example_sponza},
        {"ssr", run_example_ssr},
    };
    if (mode == "--list" || mode == "--help" || (argc > 2 && std::string_view(argv[2]) == "--help")) {
        if (mode != "--list") std::cout << "Usage: HorizonExamples [mode] (default: sponza)\n";
        for (const auto& example : examples) std::cout << example.name << '\n';
        return 0;
    }
    for (const auto& example : examples) {
        if (example.name != mode) continue;
        try {
            example.run();
            return 0;
        } catch (const std::exception& error) {
            std::cerr << mode << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cerr << "Unknown example " << mode << ". Use --list to see available modes.\n";
    return 2;
}
