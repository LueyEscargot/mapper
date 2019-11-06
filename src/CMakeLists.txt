cmake_minimum_required (VERSION 3.5)

enable_testing()

project(mapper VERSION 1.0.0 DESCRIPTION "mini port-forward server")

configure_file(project.h.in project.h)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

option(FORCE_USE_GENERIC_BUFFER "Force use generic buffer implementation" OFF)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include(GNUInstallDirs)

add_subdirectory(buffer)
aux_source_directory(. SRC_FILES)
add_executable(mapper ${SRC_FILES})
target_link_libraries(mapper PUBLIC
    Threads::Threads
    Lib_Buffer
    )
target_include_directories(mapper PUBLIC
    "${PROJECT_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/Buffer"
    )
set_target_properties(mapper
    PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# install(TARGETS mapper DESTINATION lib)
configure_file(config.ini ${CMAKE_BINARY_DIR}/config.ini COPYONLY)
