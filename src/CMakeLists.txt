cmake_minimum_required (VERSION 3.9)

project(mapper VERSION 1.0.0 DESCRIPTION "mini port-forward server")
include(GNUInstallDirs)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++0x")

file(COPY config.ini DESTINATION .)

enable_testing()

aux_source_directory(. SRC_FILES)

add_executable(mapper ${SRC_FILES})
target_link_libraries(
    mapper
    Threads::Threads
)
