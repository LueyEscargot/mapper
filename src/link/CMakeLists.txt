cmake_minimum_required (VERSION 3.5)

#------------------------------------------------------------------------------
# For Compile library Lib_Link
#------------------------------------------------------------------------------
aux_source_directory(. SRC_FILES)
add_library(Lib_Link ${SRC_FILES})

#------------------------------------------------------------------------------
# Set target directory
#------------------------------------------------------------------------------
target_include_directories(Lib_Link PUBLIC "${PROJECT_BINARY_DIR}")

#------------------------------------------------------------------------------
# Link libraries
#------------------------------------------------------------------------------
target_link_libraries(Lib_Link PUBLIC
    anl
    Lib_Buffer
    )