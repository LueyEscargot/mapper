cmake_minimum_required (VERSION 3.5)

#------------------------------------------------------------------------------
# For Compile library Lib_Buffer
#------------------------------------------------------------------------------
# if( USE_RINGBUFFER )
#     message( "-- Use Ring Buffer" )
# 	add_library(Lib_Buffer
# 		buffer.cpp
# 		ringBuffer.cpp
# 		)
# else()
#     message( "-- Use Generic Buffer" )
# 	add_library(Lib_Buffer
# 		buffer.cpp
# 		genericBuffer.cpp
# 		)
# endif()

#------------------------------------------------------------------------------
# For Compile library Lib_Buffer
#------------------------------------------------------------------------------
aux_source_directory(. SRC_FILES)
add_library(Lib_Buffer ${SRC_FILES})

#------------------------------------------------------------------------------
# Set target directory
#------------------------------------------------------------------------------
target_include_directories(Lib_Buffer PUBLIC
	"${PROJECT_BINARY_DIR}"
	)
