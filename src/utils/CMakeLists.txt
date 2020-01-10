cmake_minimum_required (VERSION 3.5)

#------------------------------------------------------------------------------
# For Compile library Lib_Utils
#------------------------------------------------------------------------------
aux_source_directory(. SRC_FILES)
add_library(Lib_Utils ${SRC_FILES})

#------------------------------------------------------------------------------
# Set target directory
#------------------------------------------------------------------------------
target_include_directories(Lib_Utils PUBLIC "${PROJECT_BINARY_DIR}")
