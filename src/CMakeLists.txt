cmake_minimum_required (VERSION 3.5)

enable_testing()

project(Mapper LANGUAGES CXX VERSION 2.0.0)

#------------------------------------------------------------------------------
# Setup compile environment
#------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include(GNUInstallDirs)

#------------------------------------------------------------------------------
# Transmit System Infomation and compile options to project
#------------------------------------------------------------------------------
message( "HOST_SYSTEM_NAME:       " ${CMAKE_HOST_SYSTEM_NAME} )
message( "HOST_SYSTEM_PROCESSOR:  " ${CMAKE_HOST_SYSTEM_PROCESSOR} )
message( "HOST_SYSTEM:            " ${CMAKE_HOST_SYSTEM} )
message( "HOST_SYSTEM_VERSION:    " ${CMAKE_HOST_SYSTEM_VERSION} )
message( "LIBRARY_ARCHITECTURE:   " ${CMAKE_LIBRARY_ARCHITECTURE} )

add_definitions( -DCMAKE_HOST_SYSTEM_NAME=${CMAKE_HOST_SYSTEM_NAME} )
add_definitions( -DCMAKE_HOST_SYSTEM_PROCESSOR=${CMAKE_HOST_SYSTEM_PROCESSOR} )
add_definitions( -DCMAKE_HOST_SYSTEM=${CMAKE_HOST_SYSTEM} )
add_definitions( -DCMAKE_HOST_SYSTEM_VERSION=${CMAKE_HOST_SYSTEM_VERSION} )
add_definitions( -DCMAKE_LIBRARY_ARCHITECTURE=${CMAKE_LIBRARY_ARCHITECTURE} )

#------------------------------------------------------------------------------
# Transmit Compile Options to project
#------------------------------------------------------------------------------
option(USE_RINGBUFFER "use Ring Buffer" OFF)
if( USE_RINGBUFFER )
    add_definitions( -DUSE_RINGBUFFER=${USE_RINGBUFFER} )
endif()

#------------------------------------------------------------------------------
# build in sub-directory: buffer
#------------------------------------------------------------------------------
add_subdirectory(buffer)

#------------------------------------------------------------------------------
# build in sub-directory: config
#------------------------------------------------------------------------------
add_subdirectory(config)

#------------------------------------------------------------------------------
# build in sub-directory: link
#------------------------------------------------------------------------------
add_subdirectory(link)

#------------------------------------------------------------------------------
# build in sub-directory: timer
#------------------------------------------------------------------------------
add_subdirectory(timer)

#------------------------------------------------------------------------------
# copy config file
#------------------------------------------------------------------------------
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.ini
    ${CMAKE_BINARY_DIR}/config.ini
    COPYONLY
    )

#------------------------------------------------------------------------------
# build main program: mapper
#------------------------------------------------------------------------------
aux_source_directory(. SRC_FILES)
list(REMOVE_ITEM SRC_FILES ./buildInfo.cpp)     # except buildInfo.cpp
add_executable(mapper ${SRC_FILES})
target_link_libraries(mapper PUBLIC
    Threads::Threads
    Lib_Buffer
    Lib_Config
    Lib_Link
    Lib_Timer
    )
target_include_directories(mapper PUBLIC
    "${PROJECT_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/buffer"
    "${PROJECT_SOURCE_DIR}/config"
    "${PROJECT_SOURCE_DIR}/link"
    "${PROJECT_SOURCE_DIR}/timer"
    )
set_target_properties(mapper
    PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

#------------------------------------------------------------------------------
# update project info file: project.h
#------------------------------------------------------------------------------
configure_file(project.h.in project.h)
set_source_files_properties (project.h.in
    PROPERTIES
    OBJECT_DEPENDS
        BUILD_INFO_SINCE
        BUILD_INFO_LAST
        BUILD_INFO_COUNT
    )

#------------------------------------------------------------------------------
# build program: buildInfo
#------------------------------------------------------------------------------
add_executable(buildInfo
    buildInfo.cpp
    )
target_link_libraries(buildInfo PUBLIC
    Lib_Config
    )

#------------------------------------------------------------------------------
# run custom command, buildInfo, to update build information
#------------------------------------------------------------------------------
add_custom_target(_always_run_target ALL
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/auto_refresh_for_increment_build_number
        ${CMAKE_CURRENT_BINARY_DIR}/project.h
    )
add_custom_command (
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/auto_refresh_for_increment_build_number
    DEPENDS
        buildInfo
    COMMAND
        buildInfo
    ARGS
        ${CMAKE_BINARY_DIR}/.buildinfo
        ${CMAKE_CURRENT_BINARY_DIR}/project.h
    )
