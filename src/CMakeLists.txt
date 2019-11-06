cmake_minimum_required (VERSION 3.5)

enable_testing()

project(Mapper VERSION 1.0.0 DESCRIPTION "mini port-forward server")

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
message( "CMAKE_HOST_SYSTEM_NAME:       " ${CMAKE_HOST_SYSTEM_NAME} )
message( "CMAKE_HOST_SYSTEM_PROCESSOR:  " ${CMAKE_HOST_SYSTEM_PROCESSOR} )
message( "CMAKE_HOST_SYSTEM:            " ${CMAKE_HOST_SYSTEM} )
message( "CMAKE_HOST_SYSTEM_VERSION:    " ${CMAKE_HOST_SYSTEM_VERSION} )
message( "CMAKE_LIBRARY_ARCHITECTURE:   " ${CMAKE_LIBRARY_ARCHITECTURE} )

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
# Setup Compile Operation
#------------------------------------------------------------------------------
configure_file(project.h.in project.h)

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
