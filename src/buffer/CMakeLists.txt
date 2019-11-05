add_library(Lib_Buffer
	buffer.cpp
	genericBuffer.cpp
	ringBuffer.cpp
	)
target_include_directories(Lib_Buffer PUBLIC
	"${PROJECT_BINARY_DIR}"
	)