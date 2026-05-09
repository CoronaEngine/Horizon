include_guard(GLOBAL)

function(corona_copy_runtime_files source_target dest_target)
    # 仅在 Windows 平台上执行
    if(NOT WIN32)
        return()
    endif()

    # 获取源目标的所有依赖库（递归）
    set(all_dependencies "")
    get_target_dependencies_recursive(${source_target} all_dependencies)
    
    # 添加源目标本身
    list(APPEND all_dependencies ${source_target})
    list(REMOVE_DUPLICATES all_dependencies)

    # 为每个依赖目标添加拷贝命令
    foreach(dep_target ${all_dependencies})
        if(TARGET ${dep_target})
            # 获取目标类型
            get_target_property(target_type ${dep_target} TYPE)
            
            # 只处理共享库和可执行文件
            if(target_type STREQUAL "SHARED_LIBRARY" OR target_type STREQUAL "MODULE_LIBRARY")
                add_custom_command(
                    TARGET ${dest_target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND}
                        -DSOURCE_DIR=$<TARGET_FILE_DIR:${dep_target}>
                        -DDEST_DIR=$<TARGET_FILE_DIR:${dest_target}>
                        -DTARGET_NAME=${dep_target}
                        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CoronaRuntimeCopy.cmake"
                    VERBATIM
                )
            endif()
        endif()
    endforeach()
endfunction()

# 递归获取目标的所有依赖
function(get_target_dependencies_recursive target out_var)
    if(NOT TARGET ${target})
        return()
    endif()

    # 获取当前目标的依赖
    get_target_property(link_libraries ${target} LINK_LIBRARIES)
    
    if(link_libraries)
        foreach(lib ${link_libraries})
            # 检查是否是目标（而不是库文件路径）
            if(TARGET ${lib})
                # 检查是否已经处理过
                list(FIND ${out_var} ${lib} found_index)
                if(found_index EQUAL -1)
                    # 添加到列表
                    list(APPEND ${out_var} ${lib})
                    set(${out_var} ${${out_var}} PARENT_SCOPE)
                    
                    # 递归处理依赖
                    get_target_dependencies_recursive(${lib} ${out_var})
                    set(${out_var} ${${out_var}} PARENT_SCOPE)
                endif()
            endif()
        endforeach()
    endif()
endfunction()