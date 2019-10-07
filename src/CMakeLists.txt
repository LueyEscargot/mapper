CMAKE_MINIMUM_REQUIRED (VERSION 3.9)

PROJECT(mapper VERSION 1.0.0 DESCRIPTION "mini port-forward server")
include(GNUInstallDirs)

SET(CMAKE_CXX_STANDARD 11)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++0x")

enable_testing()

aux_source_directory(. SRC_FILES)

add_executable(mapper ${SRC_FILES})
target_link_libraries(
    mapper
)
