project(MqChannel)

set(MqChannel_SRC_FILES
	${CMAKE_CURRENT_SOURCE_DIR}/MqChannel.cpp
)

set(MqChannel_INC_FILES
	${CMAKE_CURRENT_SOURCE_DIR}/MqChannel.h
)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(${PROJECT_NAME} STATIC ${MqChannel_SRC_FILES} ${MqChannel_INC_FILES})
