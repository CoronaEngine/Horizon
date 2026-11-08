#include "example_ibl_old/example_ibl_old.h"
#include "example_disney_pbr_old/example_disney_pbr_old.h"
#include "example_drawstress_old/example_drawstress_old.h"
#include "horizon/core/logging.h"
#include <exception>
#include <iostream>
#include <string_view>

int main(int argc, char** argv)
{
    horizon::core::LoggingOptions options;
    options.file_path = "logs/legacy_examples.log";
    options.install_signal_handlers = true;
    options.configure_utf8_console = true;
    horizon::core::initialize_logging(options);
    const std::string_view mode = argc > 1 ? argv[1] : "drawstress_old";
    const auto print_usage = [] {
        std::cerr << "Usage: HorizonExamplesOld [ibl_old|disney_pbr_old|drawstress_old] [--help]\n"
                     "HORIZON_BENCH_FRAMES=N / HORIZON_BENCH_WARMUP=N: bounded measurement\n"
                     "HORIZON_FIXED_DT=seconds: deterministic animation (not timing)\n"
                     "HORIZON_DRAWSTRESS_DIM=2..40; HORIZON_CAPTURE_PATH=output.png\n";
    };
    if (mode == "--help" || (argc > 2 && std::string_view(argv[2]) == "--help")) {
        print_usage();
        return 0;
    }
    try {
        if (mode == "ibl_old") run_example_ibl_old();
        else if (mode == "disney_pbr_old") run_example_disney_pbr_old();
        else if (mode == "drawstress_old") run_example_drawstress_old();
        else {
            std::cerr << "Unknown Legacy example: " << mode << '\n';
            print_usage();
            return 2;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Legacy example failed: " << error.what() << '\n';
        return 1;
    }
}
