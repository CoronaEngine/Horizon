# ============================================================================
# HeliconShaderCompile.cmake
# 自动扫描源码中的 #include HLSL/GLSL(path) 指令并编译 shader
# 
# 宏展开说明 (定义于 Src/Codegen/ControlFlows.h):
#   #define HELICON_STRINGIZE_(X) #X
#   #define HLSL(path) HELICON_STRINGIZE_(path.hpp)
#   #define GLSL(path) HELICON_STRINGIZE_(path.hpp)
#
# 示例:
#   #include HLSL(shaders/example.hlsl)
#   -> 预处理展开为 #include "shaders/example.hlsl.hpp"
# ============================================================================

if(NOT TARGET ShaderCompileScripts
   AND DEFINED HORIZON_SHADER_COMPILE_SCRIPTS_EXECUTABLE
   AND NOT HORIZON_SHADER_COMPILE_SCRIPTS_EXECUTABLE STREQUAL ""
   AND EXISTS "${HORIZON_SHADER_COMPILE_SCRIPTS_EXECUTABLE}")
    add_executable(ShaderCompileScripts IMPORTED GLOBAL)
    set_target_properties(ShaderCompileScripts PROPERTIES
        IMPORTED_LOCATION "${HORIZON_SHADER_COMPILE_SCRIPTS_EXECUTABLE}")
endif()

# 解析源文件中的 shader include 指令
# 返回: shader lang list, shader path list, shader relative path list
function(_helicon_parse_shader_includes source_files source_dir out_langs out_paths out_rel_paths out_source_paths out_scanned_files)
    set(_helicon_lang_list "")
    set(_helicon_path_list "")
    set(_helicon_rel_path_list "")
    set(_helicon_source_path_list "")
    set(_helicon_scanned_files "")

    foreach(_helicon_source_file ${source_files})
        # 确保是绝对路径
        if(NOT IS_ABSOLUTE "${_helicon_source_file}")
            set(_helicon_source_file "${source_dir}/${_helicon_source_file}")
        endif()

        # 只处理 C/C++ 源文件
        get_filename_component(_helicon_ext "${_helicon_source_file}" EXT)
        if(NOT _helicon_ext MATCHES "\\.(cpp|c|h|hpp|cxx|cc)$")
            continue()
        endif()

        # 检查文件是否存在
        if(NOT EXISTS "${_helicon_source_file}")
            continue()
        endif()

        # 读取文件内容
        file(READ "${_helicon_source_file}" _helicon_file_content)

        # 匹配 #include HLSL(path) 或 #include GLSL(path)
        string(REGEX MATCHALL "#include[ \t]+(HLSL|GLSL)\\([^)]+\\)" _helicon_matches "${_helicon_file_content}")

        # 记录"含 shader include 的源文件"，供调用方注册为 configure 依赖：
        # 改名/新增 shader 时编辑这些文件会自动触发重新配置(重新扫描)。
        # 只登记真正含宏的文件，避免编辑任意普通源文件都触发重配置。
        if(_helicon_matches)
            list(APPEND _helicon_scanned_files "${_helicon_source_file}")
        endif()

        foreach(_helicon_match ${_helicon_matches})
            # 提取语言类型
            string(REGEX MATCH "(HLSL|GLSL)" _helicon_lang "${_helicon_match}")
            # 提取路径（保留原始相对路径）
            string(REGEX MATCH "\\(([^)]+)\\)" _helicon_path_match "${_helicon_match}")
            string(REGEX REPLACE "^\\(|\\)$" "" _helicon_raw_shader_path "${_helicon_path_match}")
            string(STRIP "${_helicon_raw_shader_path}" _helicon_raw_shader_path)
            
            # 保存原始相对路径（用于确定输出位置）
            set(_helicon_relative_path "${_helicon_raw_shader_path}")
            
            # 处理相对路径 -> 绝对路径（用于编译）
            if(NOT IS_ABSOLUTE "${_helicon_raw_shader_path}")
                #get_filename_component(FILE_DIR "${_helicon_source_file}" DIRECTORY)
                #get_filename_component(_helicon_shader_path "${FILE_DIR}/${_helicon_raw_shader_path}" ABSOLUTE)
                get_filename_component(_helicon_shader_path "${source_dir}/${_helicon_raw_shader_path}" ABSOLUTE)
            else()
                set(_helicon_shader_path "${_helicon_raw_shader_path}")
            endif()
            
            # 规范化路径
            file(TO_CMAKE_PATH "${_helicon_shader_path}" _helicon_shader_path)
            file(TO_CMAKE_PATH "${_helicon_relative_path}" _helicon_relative_path)
            
            list(APPEND _helicon_lang_list "${_helicon_lang}")
            list(APPEND _helicon_path_list "${_helicon_shader_path}")
            list(APPEND _helicon_rel_path_list "${_helicon_relative_path}")
            list(APPEND _helicon_source_path_list "${_helicon_source_file}")
        endforeach()
    endforeach()
    
    set(${out_langs} "${_helicon_lang_list}" PARENT_SCOPE)
    set(${out_paths} "${_helicon_path_list}" PARENT_SCOPE)
    set(${out_rel_paths} "${_helicon_rel_path_list}" PARENT_SCOPE)
    set(${out_source_paths} "${_helicon_source_path_list}" PARENT_SCOPE)
    set(${out_scanned_files} "${_helicon_scanned_files}" PARENT_SCOPE)
endfunction()

# ============================================================================
# 主函数：为目标设置 shader 自动编译
# 用法: helicon_compile_shaders(TARGET [OUTPUT_DIR <dir>])
# 
# 参数:
#   TARGET      - 目标名称 (必需)
#   OUTPUT_DIR  - 输出目录 (可选, 默认: ${CMAKE_CURRENT_BINARY_DIR}/helicon_generated/<target>)
# ============================================================================
function(helicon_compile_shaders target_name)
    if(NOT TARGET ShaderCompileScripts)
        message(FATAL_ERROR
            "helicon_compile_shaders requires the ShaderCompileScripts target. "
            "When consuming Horizon as a package, build/package Horizon with with_tools=True "
            "or provide HORIZON_SHADER_COMPILE_SCRIPTS_EXECUTABLE.")
    endif()

    # 解析可选参数
    cmake_parse_arguments(_helicon_arg "" "OUTPUT_DIR" "" ${ARGN})
    
    if(NOT _helicon_arg_OUTPUT_DIR)
        set(_helicon_arg_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/helicon_generated/${target_name}")
    endif()
    
    # 确保输出目录存在
    file(MAKE_DIRECTORY "${_helicon_arg_OUTPUT_DIR}")
    
    # 获取目标的所有源文件
    get_target_property(_helicon_target_sources ${target_name} SOURCES)
    get_target_property(_helicon_target_source_dir ${target_name} SOURCE_DIR)
    
    if(NOT _helicon_target_sources)
        message(STATUS "[Helicon] ${target_name}: No sources found, skipping shader compilation")
        return()
    endif()
    
    # 解析所有源文件中的 shader include
    _helicon_parse_shader_includes("${_helicon_target_sources}" "${_helicon_target_source_dir}"
        _helicon_shader_langs _helicon_shader_paths _helicon_shader_rel_paths
        _helicon_shader_source_paths _helicon_shader_scanned_files)

    # 把"含 shader include 的源文件"注册为 configure 依赖。
    # 要生成哪些 shader 的集合在 configure 时冻结；若不监视这些文件，
    # 改名/新增 shader(只改源里的 #include GLSL(...))不会触发重新扫描，
    # 导致旧命令留下孤儿 .hpp、新 shader 永不生成(编译期 C1083)。
    # 监视后，编辑这些文件即自动重配置、重新冻结正确的命令集。
    if(_helicon_shader_scanned_files)
        set_property(DIRECTORY APPEND PROPERTY
            CMAKE_CONFIGURE_DEPENDS ${_helicon_shader_scanned_files})
    endif()

    # 计算 shader 数量
    list(LENGTH _helicon_shader_paths _helicon_shader_count)
    
    if(_helicon_shader_count EQUAL 0)
        message(STATUS "[Helicon] ${target_name}: No shader includes found")
        return()
    endif()
    
    message(STATUS "[Helicon] ${target_name}: Found ${_helicon_shader_count} shader(s) to compile")
    
    set(_helicon_generated_headers "")
    
    # 为每个 shader 创建编译命令
    math(EXPR _helicon_last_idx "${_helicon_shader_count} - 1")
    foreach(_helicon_idx RANGE ${_helicon_last_idx})
        list(GET _helicon_shader_langs ${_helicon_idx} _helicon_shader_lang)
        list(GET _helicon_shader_paths ${_helicon_idx} _helicon_shader_path)
        list(GET _helicon_shader_rel_paths ${_helicon_idx} _helicon_shader_rel_path)
        list(GET _helicon_shader_source_paths ${_helicon_idx} _helicon_shader_source_file)
        
        # 检查 shader 文件是否存在
        if(NOT EXISTS "${_helicon_shader_path}")
            message(WARNING "[Helicon] Shader file not found: ${_helicon_shader_path}")
            continue()
        endif()
        
        # 计算输出路径（保持相对路径结构）
        # 例如: shaders/example.hlsl -> ${OUTPUT_DIR}/shaders/example.hlsl.hpp
        get_filename_component(_helicon_shader_rel_dir "${_helicon_shader_rel_path}" DIRECTORY)
        get_filename_component(_helicon_shader_name "${_helicon_shader_path}" NAME)
        get_filename_component(_helicon_shader_ext "${_helicon_shader_path}" EXT)
        
        if(_helicon_shader_rel_dir)
            set(_helicon_output_subdir "${_helicon_arg_OUTPUT_DIR}/${_helicon_shader_rel_dir}")
        else()
            set(_helicon_output_subdir "${_helicon_arg_OUTPUT_DIR}")
        endif()
        
        # 确保输出子目录存在
        file(MAKE_DIRECTORY "${_helicon_output_subdir}")
        
        set(_helicon_output_header "${_helicon_output_subdir}/${_helicon_shader_name}.hpp")
        
        # 转换语言参数为小写
        string(TOLOWER "${_helicon_shader_lang}" _helicon_lang_lower)
        
        # 从文件名推断 shader stage
        get_filename_component(_helicon_shader_name_we "${_helicon_shader_path}" NAME_WE)
        string(TOLOWER "${_helicon_shader_name_we}" _helicon_shader_name_lower)
        string(TOLOWER "${_helicon_shader_ext}" _helicon_shader_ext_lower)
        
        set(_helicon_shader_stage_arg "")
        if(_helicon_shader_ext_lower MATCHES "\\.(vert|vs)")
            set(_helicon_shader_stage_arg "-t" "vert")
        elseif(_helicon_shader_ext_lower MATCHES "\\.(frag|fs)")
            set(_helicon_shader_stage_arg "-t" "frag")
        elseif(_helicon_shader_ext_lower MATCHES "\\.(comp|cs)")
            set(_helicon_shader_stage_arg "-t" "comp")
        elseif(_helicon_shader_name_lower MATCHES "frag")
            set(_helicon_shader_stage_arg "-t" "frag")
        elseif(_helicon_shader_name_lower MATCHES "comp|compute")
            set(_helicon_shader_stage_arg "-t" "comp")
        elseif(_helicon_shader_name_lower MATCHES "vert")
            set(_helicon_shader_stage_arg "-t" "vert")
        endif()
        
        # Extract only the last extension to avoid duplication with multi-dot
        # shader filenames (e.g. .comp.glsl -> .glsl, not .comp.glsl).
        string(REGEX MATCH "\\.[^.]+$" _helicon_shader_last_ext "${_helicon_shader_name}")
        if(NOT _helicon_shader_last_ext)
            set(_helicon_shader_last_ext ".h")
        endif()

        message(STATUS "[Helicon]   - ${_helicon_shader_rel_path} (${_helicon_shader_lang}) -> ${_helicon_shader_rel_path}.hpp")
        
        # 添加自定义命令（增量编译）
        add_custom_command(
            OUTPUT "${_helicon_output_header}"
            COMMAND $<TARGET_FILE:ShaderCompileScripts>
                -l ${_helicon_lang_lower}
                -s "${_helicon_shader_path}"
                -o "${_helicon_output_subdir}"
                -output-file-extension "${_helicon_shader_last_ext}.hpp"
                ${_helicon_shader_stage_arg}
            DEPENDS "${_helicon_shader_path}" ShaderCompileScripts
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "[Helicon] Compiling shader: ${_helicon_shader_rel_path}"
            VERBATIM
        )
        
        list(APPEND _helicon_generated_headers "${_helicon_output_header}")

        get_source_file_property(_helicon_existing_object_depends
            "${_helicon_shader_source_file}"
            DIRECTORY "${_helicon_target_source_dir}"
            OBJECT_DEPENDS)
        if(NOT _helicon_existing_object_depends OR
           _helicon_existing_object_depends STREQUAL "NOTFOUND")
            set(_helicon_existing_object_depends "")
        endif()

        list(APPEND _helicon_existing_object_depends "${_helicon_output_header}")
        list(REMOVE_DUPLICATES _helicon_existing_object_depends)
        set_source_files_properties("${_helicon_shader_source_file}"
            DIRECTORY "${_helicon_target_source_dir}"
            PROPERTIES OBJECT_DEPENDS "${_helicon_existing_object_depends}")
    endforeach()
    
    # 创建自定义目标
    if(_helicon_generated_headers)
        set(_helicon_shader_target "${target_name}_shaders")
        add_custom_target(${_helicon_shader_target}
            DEPENDS ${_helicon_generated_headers}
            COMMENT "[Helicon] All shaders for ${target_name} compiled"
        )
        
        # 确保 shader 在主目标之前编译
        add_dependencies(${target_name} ${_helicon_shader_target})

        set_source_files_properties(${_helicon_generated_headers}
            PROPERTIES GENERATED TRUE)
        target_sources(${target_name} PRIVATE ${_helicon_generated_headers})

        # 添加 include 路径（OUTPUT_DIR 作为根目录）
        # BEFORE: keep freshly-generated shader headers ahead of any stale
        # build-cache copies on transitive include paths so they can't be shadowed.
        target_include_directories(${target_name} BEFORE PUBLIC
            "$<BUILD_INTERFACE:${_helicon_arg_OUTPUT_DIR}>")
        
        message(STATUS "[Helicon] ${target_name}: Output directory: ${_helicon_arg_OUTPUT_DIR}")
    endif()
endfunction()
