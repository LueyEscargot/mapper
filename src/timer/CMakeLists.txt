cmake_minimum_required (VERSION 3.5)

#------------------------------------------------------------------------------
# For Compile library Lib_Timer
#------------------------------------------------------------------------------
aux_source_directory(. SRC_FILES)
add_library(Lib_Timer ${SRC_FILES})

#------------------------------------------------------------------------------
# Set target directory
#------------------------------------------------------------------------------
target_include_directories(Lib_Timer PUBLIC "${PROJECT_BINARY_DIR}")

#------------------------------------------------------------------------------
# Timer libraries
#------------------------------------------------------------------------------
target_link_libraries(Lib_Timer PUBLIC
    anl
    )