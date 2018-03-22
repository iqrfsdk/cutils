project(IqrfSpiChannel)

set(IqrfSpiChannel_SRC_FILES
	${CMAKE_CURRENT_SOURCE_DIR}/IqrfSpiChannel.cpp
)

set(IqrfSpiChannel_INC_FILES
	${CMAKE_CURRENT_SOURCE_DIR}/IqrfSpiChannel.h
)

include_directories(${clibspi_INCLUDE_DIRS})

add_library(${PROJECT_NAME} STATIC ${IqrfSpiChannel_SRC_FILES} ${IqrfSpiChannel_INC_FILES})
