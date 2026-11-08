# Build the original RHI in a separate binary/configuration; never mix APIs.
add_executable(HorizonExamplesOld
    main_old.cpp
    legacy_common.cpp
    imgui_horizon_old.cpp
    example_ibl_old/example_ibl_old.cpp
    example_disney_pbr_old/example_disney_pbr_old.cpp
    example_drawstress_old/example_drawstress_old.cpp)
target_include_directories(HorizonExamplesOld PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(HorizonExamplesOld PRIVATE
    Horizon horizon::glm horizon::glfw horizon::stb horizon_imgui)
target_compile_definitions(HorizonExamplesOld PRIVATE
    HORIZON_IMGUI_RENDER_ENABLED=$<BOOL:${HORIZON_ENABLE_IMGUI_RENDER}>)
if(HORIZON_ENABLE_TRACY)
    target_link_libraries(HorizonExamplesOld PRIVATE Tracy::TracyClient)
    target_compile_definitions(HorizonExamplesOld PRIVATE HORIZON_TRACY_ENABLED=1)
endif()
helicon_install_runtime_deps(HorizonExamplesOld)
helicon_compile_shaders(HorizonExamplesOld)
