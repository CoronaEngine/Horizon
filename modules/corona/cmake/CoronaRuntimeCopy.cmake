# 此脚本在运行时被调用，用于拷贝 DLL 和 PDB 文件
# 参数:
#   SOURCE_DIR  - 源目录（包含 DLL 和 PDB 文件）
#   DEST_DIR    - 目标目录
#   TARGET_NAME - 目标名称（用于日志）

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED DEST_DIR)
    message(FATAL_ERROR "SOURCE_DIR and DEST_DIR must be defined")
endif()

# 检查源目录是否存在
if(NOT EXISTS "${SOURCE_DIR}")
    message(WARNING "Source directory does not exist: ${SOURCE_DIR}")
    return()
endif()

# 创建目标目录（如果不存在）
file(MAKE_DIRECTORY "${DEST_DIR}")

# 收集所有 .dll 文件
file(GLOB DLL_FILES "${SOURCE_DIR}/*.dll")

# 收集所有 .pdb 文件
file(GLOB PDB_FILES "${SOURCE_DIR}/*.pdb")

set(copied_files 0)

# 拷贝 DLL 文件
foreach(file ${DLL_FILES})
    get_filename_component(filename ${file} NAME)
    set(dest_file "${DEST_DIR}/${filename}")
    
    # 检查文件是否需要拷贝
    set(need_copy FALSE)
    if(NOT EXISTS "${dest_file}")
        set(need_copy TRUE)
    else()
        file(TIMESTAMP "${file}" src_time)
        file(TIMESTAMP "${dest_file}" dest_time)
        if(NOT "${src_time}" STREQUAL "${dest_time}")
            set(need_copy TRUE)
        endif()
    endif()
    
    if(need_copy)
        if(DEFINED TARGET_NAME)
            message(STATUS "Copying ${filename} from ${TARGET_NAME} to runtime directory")
        else()
            message(STATUS "Copying ${filename} to runtime directory")
        endif()
        file(COPY_FILE "${file}" "${dest_file}" ONLY_IF_DIFFERENT)
        math(EXPR copied_files "${copied_files} + 1")
    endif()
endforeach()

# 拷贝 PDB 文件
foreach(file ${PDB_FILES})
    get_filename_component(filename ${file} NAME)
    set(dest_file "${DEST_DIR}/${filename}")
    
    # 检查文件是否需要拷贝
    set(need_copy FALSE)
    if(NOT EXISTS "${dest_file}")
        set(need_copy TRUE)
    else()
        file(TIMESTAMP "${file}" src_time)
        file(TIMESTAMP "${dest_file}" dest_time)
        if(NOT "${src_time}" STREQUAL "${dest_time}")
            set(need_copy TRUE)
        endif()
    endif()
    
    if(need_copy)
        if(DEFINED TARGET_NAME)
            message(STATUS "Copying ${filename} from ${TARGET_NAME} to runtime directory")
        else()
            message(STATUS "Copying ${filename} to runtime directory")
        endif()
        file(COPY_FILE "${file}" "${dest_file}" ONLY_IF_DIFFERENT)
        math(EXPR copied_files "${copied_files} + 1")
    endif()
endforeach()

if(copied_files GREATER 0)
    if(DEFINED TARGET_NAME)
        message(STATUS "Copied ${copied_files} runtime file(s) from ${TARGET_NAME}")
    else()
        message(STATUS "Copied ${copied_files} runtime file(s)")
    endif()
endif()
