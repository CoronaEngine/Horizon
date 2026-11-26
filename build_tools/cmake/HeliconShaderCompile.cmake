# ============================================================================
# HeliconShaderCompile.cmake
# 自动扫描源码中的 #include HLSL/GLSL/SLANG(path) 指令并编译 shader
# 
# 宏展开说明 (定义于 Src/Codegen/ControlFlows.h):
#   #define HELICON_STRINGIZE_(X) #X
#   #define HLSL(path) HELICON_STRINGIZE_(path.hpp)
#   #define GLSL(path) HELICON_STRINGIZE_(path.hpp)
#   #define SLANG(path) HELICON_STRINGIZE_(path.hpp)
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
function(_helicon_parse_shader_includes source_files source_dir output_dir out_langs out_paths out_rel_paths out_source_paths out_scanned_files)
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

        # 匹配 #include HLSL(path) 或 #include GLSL(path) 或 #include SLANG(path)
        string(REGEX MATCHALL "#include[ \t]+(HLSL|GLSL|SLANG)\\([^)]+\\)" _helicon_matches "${_helicon_file_content}")

        # 记录"含 shader include 的源文件"，供调用方注册为 configure 依赖：
        # 改名/新增 shader 时编辑这些文件会自动触发重新配置(重新扫描)。
        # 只登记真正含宏的文件，避免编辑任意普通源文件都触发重配置。
        if(_helicon_matches)
            list(APPEND _helicon_scanned_files "${_helicon_source_file}")
        endif()

        foreach(_helicon_match ${_helicon_matches})
            # 提取语言类型
            string(REGEX MATCH "(HLSL|GLSL|SLANG)" _helicon_lang "${_helicon_match}")
            # 提取路径（保留原始相对路径）
            string(REGEX MATCH "\\(([^)]+)\\)" _helicon_path_match "${_helicon_match}")
            string(REGEX REPLACE "^\\(|\\)$" "" _helicon_raw_shader_path "${_helicon_path_match}")
            string(STRIP "${_helicon_raw_shader_path}" _helicon_raw_shader_path)
            
            # 保存原始相对路径（用于确定输出位置）
            set(_helicon_relative_path "${_helicon_raw_shader_path}")
            
            # 处理相对路径 -> 绝对路径（用于编译）
            if(NOT IS_ABSOLUTE "${_helicon_raw_shader_path}")
                get_filename_component(_helicon_shader_path "${source_dir}/${_helicon_raw_shader_path}" ABSOLUTE)
                # A bare filename may refer to a shader beside the including C++ file.
                # Give each such directory its own generated include path for matching basenames.
                get_filename_component(_shader_name "${_helicon_raw_shader_path}" NAME)
                get_filename_component(_cpp_dir "${_helicon_source_file}" DIRECTORY)
                if(NOT EXISTS "${_helicon_shader_path}" AND _shader_name STREQUAL _helicon_raw_shader_path
                   AND EXISTS "${_cpp_dir}/${_shader_name}")
                    set(_helicon_shader_path "${_cpp_dir}/${_shader_name}")
                    file(RELATIVE_PATH _local_dir "${source_dir}" "${_cpp_dir}")
                    if(_local_dir MATCHES "^\\.\\." OR IS_ABSOLUTE "${_local_dir}")
                        string(SHA256 _local_dir "${_cpp_dir}")
                    endif()
                    set(_helicon_relative_path "${_local_dir}/${_shader_name}")
                    set_property(SOURCE "${_helicon_source_file}" DIRECTORY "${source_dir}" APPEND
                        PROPERTY INCLUDE_DIRECTORIES "${output_dir}/${_local_dir}")
                endif()
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

# Generate the stage header once. Paired stages also export the serialized module for reflection.
function(_helicon_compile_stage source header language stage module_output dependencies)
    get_filename_component(_directory "${header}" DIRECTORY)
    get_filename_component(_extension "${source}" LAST_EXT)
    file(MAKE_DIRECTORY "${_directory}")

    if(NOT stage)
        # Match the stage hints used for standalone HLSL/GLSL/SLANG includes.
        get_filename_component(_stem "${source}" NAME_WE)
        get_filename_component(_ext "${source}" EXT)
        string(TOLOWER "${_stem}" _stem)
        string(TOLOWER "${_ext}" _ext)
        if(_ext MATCHES "\\.(vert|vs)")
            set(stage vert)
        elseif(_ext MATCHES "\\.(frag|fs)")
            set(stage frag)
        elseif(_ext MATCHES "\\.(comp|cs)")
            set(stage comp)
        elseif(_stem MATCHES "frag")
            set(stage frag)
        elseif(_stem MATCHES "comp|compute")
            set(stage comp)
        elseif(_stem MATCHES "vert")
            set(stage vert)
        endif()
    endif()
    set(_options)
    set(_outputs "${header}")
    if(stage)
        list(APPEND _options -t "${stage}")
    endif()
    if(module_output)
        get_filename_component(_module_directory "${module_output}" DIRECTORY)
        file(MAKE_DIRECTORY "${_module_directory}")
        list(APPEND _options -module-output "${module_output}")
        list(APPEND _outputs "${module_output}")
    endif()
    string(TOLOWER "${language}" language)
    add_custom_command(
        OUTPUT ${_outputs}
        COMMAND $<TARGET_FILE:ShaderCompileScripts> -l "${language}" -s "${source}" -o "${_directory}"
                -output-file-extension "${_extension}.hpp" ${_options} ${ARGN}
        DEPENDS "${source}" ${dependencies} ShaderCompileScripts
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "[Helicon] Compiling shader: ${source}"
        VERBATIM)
endfunction()

# RASTER_PAIRS takes triples: generated header (relative to OUTPUT_DIR), vertex source, fragment source.
# Shader sources are relative to the target's source directory or absolute.
function(helicon_compile_shaders target_name)
    if(NOT TARGET ShaderCompileScripts)
        message(FATAL_ERROR
            "helicon_compile_shaders requires the ShaderCompileScripts target. "
            "When consuming Horizon as a package, build/package Horizon with with_tools=True "
            "or provide HORIZON_SHADER_COMPILE_SCRIPTS_EXECUTABLE.")
    endif()

    cmake_parse_arguments(_args "" "OUTPUT_DIR" "DEPENDS;RASTER_PAIRS" ${ARGN})
    if(NOT _args_OUTPUT_DIR)
        set(_args_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/helicon_generated/${target_name}")
    endif()
    cmake_path(ABSOLUTE_PATH _args_OUTPUT_DIR BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" NORMALIZE)
    get_target_property(_sources ${target_name} SOURCES)
    get_target_property(_source_dir ${target_name} SOURCE_DIR)
    _helicon_parse_shader_includes("${_sources}" "${_source_dir}" "${_args_OUTPUT_DIR}"
        _languages _shader_paths _relative_paths _source_files _scanned_files)
    if(_scanned_files)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_scanned_files})
    endif()

    set(_stage_headers)
    set(_pair_headers)
    set(_pairs)
    set(_paired_ids)
    set(_stages vert frag)
    set(_partners frag vert)
    list(LENGTH _args_RASTER_PAIRS _count)
    math(EXPR _remainder "${_count} % 3")
    if(_remainder)
        message(FATAL_ERROR "RASTER_PAIRS expects triples of output header, vertex shader, fragment shader")
    endif()
    while(_args_RASTER_PAIRS)
        list(POP_FRONT _args_RASTER_PAIRS _pair_header _vert _frag)
        cmake_path(NORMAL_PATH _pair_header)
        if(IS_ABSOLUTE "${_pair_header}" OR _pair_header MATCHES "^\\.\\.(/|$)")
            message(FATAL_ERROR "Raster pair header must be relative to OUTPUT_DIR: ${_pair_header}")
        endif()
        set(_output "${_args_OUTPUT_DIR}/${_pair_header}")
        get_filename_component(_directory "${_output}" DIRECTORY)
        cmake_path(ABSOLUTE_PATH _vert BASE_DIRECTORY "${_source_dir}" NORMALIZE)
        cmake_path(ABSOLUTE_PATH _frag BASE_DIRECTORY "${_source_dir}" NORMALIZE)
        foreach(_stage _partner IN ZIP_LISTS _stages _partners)
            set(_source "${_${_stage}}")
            if(NOT EXISTS "${_source}")
                message(FATAL_ERROR "Raster ${_stage} shader does not exist: ${_source}")
            endif()
            string(SHA256 _id "${_source}")
            if(_id IN_LIST _paired_ids AND NOT _role_${_id} STREQUAL _stage)
                message(FATAL_ERROR "Shader registered as both vertex and fragment stage: ${_source}")
            endif()
            if(NOT _id IN_LIST _paired_ids)
                file(RELATIVE_PATH _relative "${_source_dir}" "${_source}")
                if(IS_ABSOLUTE "${_relative}" OR _relative MATCHES "^\\.\\./")
                    file(RELATIVE_PATH _relative "${PROJECT_SOURCE_DIR}" "${_source}")
                endif()
                if(IS_ABSOLUTE "${_relative}" OR _relative MATCHES "^\\.\\./")
                    get_filename_component(_dir "${_source}" DIRECTORY)
                    get_filename_component(_name "${_source}" NAME)
                    string(SHA256 _external_dir "${_dir}")
                    set(_relative "external/${_external_dir}/${_name}")
                endif()
                set(_header_${_id} "${_args_OUTPUT_DIR}/${_relative}.hpp")
                set(_ir_${_id} "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${target_name}.dir/slang/${_relative}.slang-module")
                if(_header_${_id} IN_LIST _pair_headers OR _header_${_id} IN_LIST _stage_headers)
                    message(FATAL_ERROR "Duplicate generated shader header: ${_header_${_id}}")
                endif()
                set(_source_${_id} "${_source}")
                set(_role_${_id} "${_stage}")
                list(APPEND _paired_ids "${_id}")
                list(APPEND _stage_headers "${_header_${_id}}")
            endif()
            get_filename_component(_stage_dir "${_header_${_id}}" DIRECTORY)
            file(RELATIVE_PATH _pair_include "${_stage_dir}" "${_output}")
            string(SHA256 _partner_id "${_${_partner}}")
            list(APPEND _partners_${_id} -raster-partner "HORIZON_HELICON_STAGE_${_partner_id}" "${_pair_include}")
            file(RELATIVE_PATH _${_stage}_header "${_directory}" "${_header_${_id}}")
            set(_${_stage}_ir "${_ir_${_id}}")
        endforeach()

        set(_pair "${_vert}|${_frag}")
        if(_output IN_LIST _stage_headers OR _output IN_LIST _pair_headers OR _pair IN_LIST _pairs)
            message(FATAL_ERROR "Duplicate raster pair or generated header: ${_pair_header} (${_vert}, ${_frag})")
        endif()
        file(MAKE_DIRECTORY "${_directory}")
        add_custom_command(
            OUTPUT "${_output}"
            COMMAND $<TARGET_FILE:ShaderCompileScripts> -raster-pair -o "${_output}"
                    -vs "${_vert}" -fs "${_frag}" -vs-header "${_vert_header}" -fs-header "${_frag_header}"
                    -vs-ir "${_vert_ir}" -fs-ir "${_frag_ir}"
            DEPENDS "${_vert_ir}" "${_frag_ir}" ShaderCompileScripts
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "[Helicon] Generating raster pipeline: ${_pair_header}"
            VERBATIM)
        list(APPEND _pair_headers "${_output}")
        list(APPEND _pairs "${_pair}")
    endwhile()

    # A shared stage needs every partner before its single compilation command is declared.
    foreach(_id IN LISTS _paired_ids)
        set(_source "${_source_${_id}}")
        list(FIND _shader_paths "${_source}" _scanned)
        cmake_path(GET _source EXTENSION LAST_ONLY _ext)
        string(TOLOWER "${_ext}" _ext)
        if(_scanned GREATER_EQUAL 0)
            list(GET _languages ${_scanned} _language)
        elseif(_ext STREQUAL ".slang")
            set(_language slang)
        elseif(_ext STREQUAL ".hlsl")
            set(_language hlsl)
        else()
            set(_language glsl)
        endif()
        _helicon_compile_stage("${_source}" "${_header_${_id}}" "${_language}" "${_role_${_id}}" "${_ir_${_id}}"
            "${_args_DEPENDS}" -raster-ready "HORIZON_HELICON_STAGE_${_id}" ${_partners_${_id}})
    endforeach()

    foreach(_language _shader _relative _consumer IN ZIP_LISTS _languages _shader_paths _relative_paths _source_files)
        if(NOT EXISTS "${_shader}")
            continue()
        endif()
        set(_header "${_args_OUTPUT_DIR}/${_relative}.hpp")
        if(_header IN_LIST _pair_headers)
            message(FATAL_ERROR "Raster pair and stage generate the same header: ${_header}")
        endif()
        if(NOT _header IN_LIST _stage_headers)
            _helicon_compile_stage("${_shader}" "${_header}" "${_language}" "" "" "${_args_DEPENDS}")
            list(APPEND _stage_headers "${_header}")
        endif()
        set_property(SOURCE "${_consumer}" DIRECTORY "${_source_dir}" APPEND PROPERTY OBJECT_DEPENDS "${_header}")
    endforeach()

    set(_headers ${_stage_headers} ${_pair_headers})
    if(_headers)
        add_custom_target(${target_name}_shaders DEPENDS ${_headers})
        add_dependencies(${target_name} ${target_name}_shaders)
        target_sources(${target_name} PRIVATE ${_headers})
        target_include_directories(${target_name} BEFORE PUBLIC "$<BUILD_INTERFACE:${_args_OUTPUT_DIR}>")
    endif()
endfunction()
