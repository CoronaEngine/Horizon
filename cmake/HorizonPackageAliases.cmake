include_guard(GLOBAL)

get_filename_component(_horizon_package_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(TARGET corona_kernel AND NOT TARGET corona::kernel)
    add_library(corona::kernel INTERFACE IMPORTED)
    target_link_libraries(corona::kernel INTERFACE corona_kernel)
endif()

if(TARGET Helicon)
    if(WIN32)
        file(GLOB _horizon_helicon_runtime_deps CONFIGURE_DEPENDS "${_horizon_package_root}/bin/*.dll")
    elseif(APPLE)
        file(GLOB _horizon_helicon_runtime_deps CONFIGURE_DEPENDS "${_horizon_package_root}/bin/*.dylib")
    else()
        file(GLOB _horizon_helicon_runtime_deps CONFIGURE_DEPENDS "${_horizon_package_root}/bin/*.so*")
    endif()

    if(_horizon_helicon_runtime_deps)
        set_target_properties(Helicon PROPERTIES
            INTERFACE_HELICON_RUNTIME_DEPS "${_horizon_helicon_runtime_deps}"
        )
    endif()
endif()
