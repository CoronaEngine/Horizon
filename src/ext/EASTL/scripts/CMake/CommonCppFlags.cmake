#-------------------------------------------------------------------------------------------
# Compiler Flag Detection
#-------------------------------------------------------------------------------------------
include(CheckCXXCompilerFlag)

check_cxx_compiler_flag("-fchar8_t" EASTL_HAS_FCHAR8T_FLAG)
check_cxx_compiler_flag("/Zc:char8_t" EASTL_HAS_ZCCHAR8T_FLAG)

if(EASTL_HAS_FCHAR8T_FLAG)
    set(EASTL_CHAR8T_FLAG "-fchar8_t")
    set(EASTL_NO_CHAR8T_FLAG "-fno-char8_t")
elseif(EASTL_HAS_ZCCHAR8T_FLAG)
    set(EASTL_CHAR8T_FLAG "/Zc:char8_t")
    set(EASTL_NO_CHAR8T_FLAG "/Zc:char8_t-")
endif()

#-------------------------------------------------------------------------------------------
# Compiler Flags
#-------------------------------------------------------------------------------------------
if(UNIX AND "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Intel" )
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fasm-blocks" )
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /permissive-")
endif()


if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -D_DEBUG")
endif()
