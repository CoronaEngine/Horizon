include(CMakeDependentOption)

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(HORIZON_BUILD_ENGINE "Build Helicon and the complete Horizon engine target" ON)
option(HORIZON_BUILD_TOOLS "Build Horizon tool executables" OFF)
option(HORIZON_BUILD_EXAMPLES "Build Horizon example executables" ${PROJECT_IS_TOP_LEVEL})
option(HORIZON_BUILD_TESTS "Build Horizon correctness tests" ON)
cmake_dependent_option(HORIZON_BUILD_OCARINA
    "Build the bundled Ocarina module and Vulkan compute backend" OFF
    "NOT HORIZON_BUILD_EXAMPLES" ON)
option(HORIZON_BUILD_OCARINA_TESTS "Build bundled ocarina test executables" OFF)
option(HORIZON_BUILD_TRACE_MODE_TESTS "Build local Ocarina GPU trace-mode comparisons" OFF)
option(HORIZON_BUILD_WARP32_TESTS "Register local Ocarina GPU warp32 tests" OFF)
option(HORIZON_ENABLE_HARDCODE_SHADERS "Compile and use generated Helicon hardcoded shader cache" OFF)
option(HORIZON_ENABLE_TRACY "Enable Tracy profiler instrumentation" ON)

if(NOT HORIZON_BUILD_ENGINE AND
   (HORIZON_BUILD_TOOLS OR HORIZON_BUILD_EXAMPLES OR HORIZON_BUILD_OCARINA))
    message(FATAL_ERROR
        "HORIZON_BUILD_ENGINE=OFF is incompatible with tools, examples, and Ocarina targets")
endif()
