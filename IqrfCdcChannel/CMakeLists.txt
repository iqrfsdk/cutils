project(IqrfCdcChannel)

set(IqrfCdcChannel_SRC_FILES
	${CMAKE_CURRENT_SOURCE_DIR}/IqrfCdcChannel.cpp
)

set(IqrfCdcChannel_INC_FILES
	${CMAKE_CURRENT_SOURCE_DIR}/IqrfCdcChannel.h
)

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${clibcdc_INCLUDE_DIRS})

message("clibcdc_INCLUDE_DIRS: ${clibcdc_INCLUDE_DIRS}")

add_library(${PROJECT_NAME} STATIC ${IqrfCdcChannel_SRC_FILES} ${IqrfCdcChannel_INC_FILES})
