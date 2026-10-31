
find_package(CUDAToolkit)
if (CUDAToolkit_FOUND)

    file(GLOB_RECURSE backend_headers "builtin/*.h")

    set(BUILTIN_SRC "${CMAKE_CURRENT_SOURCE_DIR}/builtin")
    # User manual target to copy CUDA header files
    add_custom_target(copy_cuda_headers ALL
        COMMAND ${CMAKE_COMMAND} -E echo "Copying cuda header files to $<TARGET_FILE_DIR:ocarina-backend-cuda>/cuda"
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ocarina-backend-cuda>/cuda
        COMMAND ${PYTHON_PATH} ${BUILTIN_SRC}/copy_cuda_files.py $<TARGET_FILE_DIR:ocarina-backend-cuda>/cuda
        COMMAND ${CMAKE_COMMAND} -E echo "Complete copy!"
        DEPENDS ocarina-backend-cuda ${backend_headers}
    )

    include_directories(${CUDA_INCLUDE_DIRS})
    include_directories(builtin/optix)
    file(GLOB_RECURSE HEADER_FILES *.h*)
    file(GLOB_RECURSE SOURCE_FILES *.c*)
    ocarina_add_backend(cuda SOURCES ${HEADER_FILES} ${SOURCE_FILES})
    target_link_libraries(ocarina-backend-cuda PRIVATE CUDA::cuda_driver CUDA::nvrtc CUDA::cudart)
else ()
    message(STATUS "CUDAToolkit not found, skipping CUDA backend build.")
endif ()

