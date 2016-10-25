project(UdpChannel)

set(UdpChannel_SRC_FILES
	${CMAKE_CURRENT_SOURCE_DIR}/UdpChannel.cpp
)

set(UdpChannel_INC_FILES
	${CMAKE_CURRENT_SOURCE_DIR}/UdpChannel.h
)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(${PROJECT_NAME} STATIC ${UdpChannel_SRC_FILES} ${UdpChannel_INC_FILES})
