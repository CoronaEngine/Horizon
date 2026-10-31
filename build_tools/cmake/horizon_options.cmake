option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(HORIZON_BUILD_ENGINE "Build Helicon and the complete Horizon engine target" ON)
option(HORIZON_BUILD_TOOLS "Build Horizon tool executables" OFF)
option(HORIZON_BUILD_EXAMPLES "Build Horizon example executables" ${PROJECT_IS_TOP_LEVEL})
option(HORIZON_BUILD_TESTS "Build Horizon correctness tests" ON)
option(HORIZON_BUILD_OCARINA "Build the bundled ocarina module and available GPU backends" OFF)
option(HORIZON_BUILD_OCARINA_TESTS "Build bundled ocarina test executables" OFF)
option(HORIZON_BUILD_VISION_HOTFIX "Build Vision hotfix support targets" OFF)
option(HORIZON_ENABLE_HARDCODE_SHADERS "Compile and use generated Helicon hardcoded shader cache" OFF)
option(HORIZON_ENABLE_TRACY "Enable Tracy profiler instrumentation" ON)
option(HORIZON_ENABLE_IMGUI_RENDER "Render the ImGui overlay in Horizon examples" ON)

if(NOT HORIZON_BUILD_ENGINE AND
   (HORIZON_BUILD_TOOLS OR HORIZON_BUILD_EXAMPLES OR HORIZON_BUILD_OCARINA OR HORIZON_BUILD_VISION_HOTFIX))
    message(FATAL_ERROR
        "HORIZON_BUILD_ENGINE=OFF is incompatible with tools, examples, Ocarina, and Vision hotfix targets")
endif()
