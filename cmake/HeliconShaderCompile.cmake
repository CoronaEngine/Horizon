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

# 解析源文件中的 shader include 指令
# 返回: SHADER_LANG_LIST, SHADER_PATH_LIST, SHADER_RELATIVE_PATH_LIST
function(_helicon_parse_shader_includes SOURCE_FILES SOURCE_DIR OUT_LANGS OUT_PATHS OUT_REL_PATHS)
    set(LANG_LIST "")
    set(PATH_LIST "")
    set(REL_PATH_LIST "")
    
    foreach(SOURCE_FILE ${SOURCE_FILES})
        # 确保是绝对路径
        if(NOT IS_ABSOLUTE "${SOURCE_FILE}")
            set(SOURCE_FILE "${SOURCE_DIR}/${SOURCE_FILE}")
        endif()
        
        # 只处理 C/C++ 源文件
        get_filename_component(EXT "${SOURCE_FILE}" EXT)
        if(NOT EXT MATCHES "\\.(cpp|c|h|hpp|cxx|cc)$")
            continue()
        endif()
        
        # 检查文件是否存在
        if(NOT EXISTS "${SOURCE_FILE}")
            continue()
        endif()
        
        # 读取文件内容
        file(READ "${SOURCE_FILE}" FILE_CONTENT)
        
        # 匹配 #include HLSL(path) 或 #include GLSL(path)
        string(REGEX MATCHALL "#include[ \t]+(HLSL|GLSL)\\([^)]+\\)" MATCHES "${FILE_CONTENT}")
        
        foreach(MATCH ${MATCHES})
            # 提取语言类型
            string(REGEX MATCH "(HLSL|GLSL)" LANG "${MATCH}")
            # 提取路径（保留原始相对路径）
            string(REGEX MATCH "\\(([^)]+)\\)" PATH_MATCH "${MATCH}")
            string(REGEX REPLACE "^\\(|\\)$" "" RAW_SHADER_PATH "${PATH_MATCH}")
            string(STRIP "${RAW_SHADER_PATH}" RAW_SHADER_PATH)
            
            # 保存原始相对路径（用于确定输出位置）
            set(RELATIVE_PATH "${RAW_SHADER_PATH}")
            
            # 处理相对路径 -> 绝对路径（用于编译）
            if(NOT IS_ABSOLUTE "${RAW_SHADER_PATH}")
                get_filename_component(FILE_DIR "${SOURCE_FILE}" DIRECTORY)
                get_filename_component(SHADER_PATH "${FILE_DIR}/${RAW_SHADER_PATH}" ABSOLUTE)
            else()
                set(SHADER_PATH "${RAW_SHADER_PATH}")
            endif()
            
            # 规范化路径
            file(TO_CMAKE_PATH "${SHADER_PATH}" SHADER_PATH)
            file(TO_CMAKE_PATH "${RELATIVE_PATH}" RELATIVE_PATH)
            
            list(APPEND LANG_LIST "${LANG}")
            list(APPEND PATH_LIST "${SHADER_PATH}")
            list(APPEND REL_PATH_LIST "${RELATIVE_PATH}")
        endforeach()
    endforeach()
    
    set(${OUT_LANGS} "${LANG_LIST}" PARENT_SCOPE)
    set(${OUT_PATHS} "${PATH_LIST}" PARENT_SCOPE)
    set(${OUT_REL_PATHS} "${REL_PATH_LIST}" PARENT_SCOPE)
endfunction()

# ============================================================================
# 主函数：为目标设置 shader 自动编译
# 用法: helicon_compile_shaders(TARGET [OUTPUT_DIR <dir>])
# 
# 参数:
#   TARGET      - 目标名称 (必需)
#   OUTPUT_DIR  - 输出目录 (可选, 默认: ${PROJECT_SOURCE_DIR}/Src/Helicon/Compiler/HardcodeShaders)
# ============================================================================
function(helicon_compile_shaders TARGET_NAME)
    # 解析可选参数
    cmake_parse_arguments(ARG "" "OUTPUT_DIR" "" ${ARGN})
    
    if(NOT ARG_OUTPUT_DIR)
        set(ARG_OUTPUT_DIR "${PROJECT_SOURCE_DIR}/src/Helicon/Compiler/HardcodeShaders")
    endif()
    
    # 确保输出目录存在
    file(MAKE_DIRECTORY "${ARG_OUTPUT_DIR}")
    
    # 获取目标的所有源文件
    get_target_property(TARGET_SOURCES ${TARGET_NAME} SOURCES)
    get_target_property(TARGET_SOURCE_DIR ${TARGET_NAME} SOURCE_DIR)
    
    if(NOT TARGET_SOURCES)
        message(STATUS "[Helicon] ${TARGET_NAME}: No sources found, skipping shader compilation")
        return()
    endif()
    
    # 解析所有源文件中的 shader include
    _helicon_parse_shader_includes("${TARGET_SOURCES}" "${TARGET_SOURCE_DIR}" 
        SHADER_LANGS SHADER_PATHS SHADER_REL_PATHS)
    
    # 计算 shader 数量
    list(LENGTH SHADER_PATHS SHADER_COUNT)
    
    if(SHADER_COUNT EQUAL 0)
        message(STATUS "[Helicon] ${TARGET_NAME}: No shader includes found")
        return()
    endif()
    
    message(STATUS "[Helicon] ${TARGET_NAME}: Found ${SHADER_COUNT} shader(s) to compile")
    
    set(ALL_GENERATED_HEADERS "")
    
    # 为每个 shader 创建编译命令
    math(EXPR LAST_IDX "${SHADER_COUNT} - 1")
    foreach(IDX RANGE ${LAST_IDX})
        list(GET SHADER_LANGS ${IDX} SHADER_LANG)
        list(GET SHADER_PATHS ${IDX} SHADER_PATH)
        list(GET SHADER_REL_PATHS ${IDX} SHADER_REL_PATH)
        
        # 检查 shader 文件是否存在
        if(NOT EXISTS "${SHADER_PATH}")
            message(WARNING "[Helicon] Shader file not found: ${SHADER_PATH}")
            continue()
        endif()
        
        # 计算输出路径（保持相对路径结构）
        # 例如: shaders/example.hlsl -> ${OUTPUT_DIR}/shaders/example.hlsl.hpp
        get_filename_component(SHADER_REL_DIR "${SHADER_REL_PATH}" DIRECTORY)
        get_filename_component(SHADER_NAME "${SHADER_PATH}" NAME)
        get_filename_component(SHADER_EXT "${SHADER_PATH}" EXT)
        
        if(SHADER_REL_DIR)
            set(OUTPUT_SUBDIR "${ARG_OUTPUT_DIR}/${SHADER_REL_DIR}")
        else()
            set(OUTPUT_SUBDIR "${ARG_OUTPUT_DIR}")
        endif()
        
        # 确保输出子目录存在
        file(MAKE_DIRECTORY "${OUTPUT_SUBDIR}")
        
        set(OUTPUT_HEADER "${OUTPUT_SUBDIR}/${SHADER_NAME}.hpp")
        
        # 转换语言参数为小写
        string(TOLOWER "${SHADER_LANG}" LANG_LOWER)
        
        # 从文件名推断 shader stage
        get_filename_component(SHADER_NAME_WE "${SHADER_PATH}" NAME_WE)
        string(TOLOWER "${SHADER_NAME_WE}" SHADER_NAME_LOWER)
        string(TOLOWER "${SHADER_EXT}" SHADER_EXT_LOWER)
        
        set(SHADER_STAGE_ARG "")
        if(SHADER_EXT_LOWER MATCHES "\\.(vert|vs)")
            set(SHADER_STAGE_ARG "-t" "vert")
        elseif(SHADER_EXT_LOWER MATCHES "\\.(frag|fs)")
            set(SHADER_STAGE_ARG "-t" "frag")
        elseif(SHADER_EXT_LOWER MATCHES "\\.(comp|cs)")
            set(SHADER_STAGE_ARG "-t" "comp")
        elseif(SHADER_NAME_LOWER MATCHES "frag")
            set(SHADER_STAGE_ARG "-t" "frag")
        elseif(SHADER_NAME_LOWER MATCHES "comp|compute")
            set(SHADER_STAGE_ARG "-t" "comp")
        elseif(SHADER_NAME_LOWER MATCHES "vert")
            set(SHADER_STAGE_ARG "-t" "vert")
        endif()
        
        # Extract only the last extension to avoid duplication with multi-dot
        # shader filenames (e.g. .comp.glsl -> .glsl, not .comp.glsl).
        string(REGEX MATCH "\\.[^.]+$" SHADER_LAST_EXT "${SHADER_NAME}")
        if(NOT SHADER_LAST_EXT)
            set(SHADER_LAST_EXT ".h")
        endif()

        message(STATUS "[Helicon]   - ${SHADER_REL_PATH} (${SHADER_LANG}) -> ${SHADER_REL_PATH}.hpp")
        
        # 添加自定义命令（增量编译）
        add_custom_command(
            OUTPUT "${OUTPUT_HEADER}"
            COMMAND $<TARGET_FILE:ShaderCompileScripts>
                -l ${LANG_LOWER}
                -s "${SHADER_PATH}"
                -o "${OUTPUT_SUBDIR}"
                -output-file-extension "${SHADER_LAST_EXT}.hpp"
                ${SHADER_STAGE_ARG}
            DEPENDS "${SHADER_PATH}" ShaderCompileScripts
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "[Helicon] Compiling shader: ${SHADER_REL_PATH}"
            VERBATIM
        )
        
        list(APPEND ALL_GENERATED_HEADERS "${OUTPUT_HEADER}")
    endforeach()
    
    # 创建自定义目标
    if(ALL_GENERATED_HEADERS)
        set(SHADER_TARGET "${TARGET_NAME}_shaders")
        add_custom_target(${SHADER_TARGET}
            DEPENDS ${ALL_GENERATED_HEADERS}
            COMMENT "[Helicon] All shaders for ${TARGET_NAME} compiled"
        )
        
        # 确保 shader 在主目标之前编译
        add_dependencies(${TARGET_NAME} ${SHADER_TARGET})
        
        # 添加 include 路径（OUTPUT_DIR 作为根目录）
        target_include_directories(${TARGET_NAME} PUBLIC "${ARG_OUTPUT_DIR}")
        
        message(STATUS "[Helicon] ${TARGET_NAME}: Output directory: ${ARG_OUTPUT_DIR}")
    endif()
endfunction()
