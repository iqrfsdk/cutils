project(IqrfFakeChannel)

set(IqrfFakeChannel_SRC_FILES
	${CMAKE_CURRENT_SOURCE_DIR}/IqrfFakeChannel.cpp
)

set(IqrfFakeChannel_INC_FILES
	${CMAKE_CURRENT_SOURCE_DIR}/IqrfFakeChannel.h
)

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${clibcdc_INCLUDE_DIRS})

add_library(${PROJECT_NAME} STATIC ${IqrfFakeChannel_SRC_FILES} ${IqrfFakeChannel_INC_FILES})
