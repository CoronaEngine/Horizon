

option(AUTO_LOCATE_VULKAN "AUTO_LOCATE_VULKAN" ON)

if(AUTO_LOCATE_VULKAN)
	message(STATUS "Attempting auto locate Vulkan using CMake......")
	
	# Find Vulkan Path using CMake's Vulkan Module
	# This will return Boolean 'Vulkan_FOUND' indicating the status of find as success(ON) or fail(OFF).
	# Include directory path - 'Vulkan_INCLUDE_DIRS' and 'Vulkan_LIBRARY' with required libraries.
	find_package(Vulkan)
	
	# Try extracting VulkanSDK path from ${Vulkan_INCLUDE_DIRS}
	if (NOT ${Vulkan_INCLUDE_DIRS} STREQUAL "")
		set(VULKAN_PATH ${Vulkan_INCLUDE_DIRS})
		STRING(REGEX REPLACE "/Include" "" VULKAN_PATH ${VULKAN_PATH})
	endif()
		 
	if(NOT Vulkan_FOUND)
		# CMake may fail to locate the libraries but could be able to 
		# provide some path in Vulkan SDK include directory variable
		# 'Vulkan_INCLUDE_DIRS', try to extract path from this.
		message(STATUS "Failed to locate Vulkan SDK, retrying again...")
		if(EXISTS "${VULKAN_PATH}")
			message(STATUS "Successfully located the Vulkan SDK: ${VULKAN_PATH}")
		else()
			message("Error: Unable to locate Vulkan SDK. Please turn off auto locate option by specifying 'AUTO_LOCATE_VULKAN' as 'OFF'")
			message("and specify manually path using 'VULKAN_SDK' and 'VULKAN_VERSION' variables in the CMakeLists.txt.")
			return()
		endif()
	endif()

	file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin/vulkan)

    file(GLOB_RECURSE HEADER_FILES *.h*)
    file(GLOB_RECURSE SOURCE_FILES *.c*)

	foreach(file ${HEADER_FILES})
		# Get the file extension
		get_filename_component(file_ext ${file} EXT)
		message(STATUS "get_filename_component in vulkan backend project : ${file}, ext is ${file_ext}")
		# Check if the extension is not "hlsl" and remove it from the list
		if(file_ext STREQUAL ".hlsl")
			message("ext equals to hlsl: ${file}")
			list(REMOVE_ITEM HEADER_FILES ${file})
		endif()
	endforeach()

    ocarina_add_backend(vulkan SOURCES ${HEADER_FILES} ${SOURCE_FILES})
    #target_compile_definitions(ocarina-backend-vulkan PRIVATE OPTIX_INCLUDE=${OptiX_INCLUDE})
    target_link_libraries(ocarina-backend-vulkan PRIVATE Vulkan::Vulkan)
else()
	message(STATUS "Attempting to locate Vulkan SDK using manual path......")
	set(VULKAN_SDK "C:/VulkanSDK")
	set(VULKAN_VERSION "1.3.268.0")
	set(VULKAN_PATH "${VULKAN_SDK}/${VULKAN_VERSION}")
	message(STATUS "Using manual specified path: ${VULKAN_PATH}")

	# Check if manual set path exists
	if(NOT EXISTS "${VULKAN_PATH}")
		message("Error: Unable to locate this Vulkan SDK path VULKAN_PATH: ${VULKAN_PATH}, please specify correct path.
		For more information on correct installation process, please refer to subsection 'Getting started with Lunar-G SDK'
		and 'Setting up first project with CMake' in Chapter 3, 'Shaking hands with the device' in this book 'Learning Vulkan', ISBN - 9781786469809.")
	   return()
	endif()
endif()

set(BUILTIN_DST "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_CFG_INTDIR}")


if(MSVC)
    set(DXC_PATH "../../ext/dxc/msvc")
	set(SPIRV_PATH "../../ext/SPIRV-Cross")
	target_include_directories(ocarina-backend-vulkan PRIVATE ${DXC_PATH}/inc ${SPIRV_PATH})

	set(DXC_LIB ${DXC_PATH}/lib/x64)
	set(DXC_BIN ${DXC_PATH}/bin/x64)
	if (NOT CMAKE_CL_64)
		set(DXC_LIB ${DXC_PATH}/lib/x86)
		set(DXC_BIN ${DXC_PATH}/bin/x86)
	endif()
	message("set DXC library path: ${DXC_LIB}")

	link_directories(${DXC_LIB})
	target_link_libraries(ocarina-backend-vulkan PRIVATE dxcompiler.lib spirv-cross-core spirv-cross-hlsl)

	file(GLOB DXC_BIN_FILES "${DXC_BIN}/*.*")

	foreach(FILE ${DXC_BIN_FILES})
		add_custom_command(TARGET ocarina-backend-vulkan POST_BUILD        # Adds a post-build event to MyTest
		COMMAND ${CMAKE_COMMAND} -E copy  # which executes "cmake - E copy..."
			"${FILE}"      # <--this is in-file
			"${BUILTIN_DST}")                 # <--this is out-file path
	endforeach()
else()
	set(DXC_PATH "../../ext/dxc/clang")
	include_directories(${DXC_PATH}/inc)
	add_library(dxc SHARED IMPORTED)
	target_link_libraries(ocarina-backend-vulkan PRIVATE dxc)
endif()
