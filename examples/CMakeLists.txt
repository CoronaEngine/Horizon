set(HORIZON_EXAMPLE_SOURCES
    main.cpp
    example_1/example_1.cpp
    example_2/example_2.cpp
    example_3/example_3.cpp)

set(HORIZON_EXAMPLE_HEADERS
    example_1/example_1.hpp
    example_2/example_2.hpp
    example_3/example_3.hpp)

add_executable(default ${HORIZON_EXAMPLE_SOURCES} ${HORIZON_EXAMPLE_HEADERS})

target_include_directories(default PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/example_1"
    "${CMAKE_CURRENT_SOURCE_DIR}/example_2"
    "${CMAKE_CURRENT_SOURCE_DIR}/example_3")

set_target_properties(default PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:default>")
