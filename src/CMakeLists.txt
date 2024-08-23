cmake_minimum_required(VERSION 3.18)
INCLUDE (CheckIncludeFiles)
INCLUDE(FindPkgConfig)

PROJECT(wireless-rate-limiter C)

SET(SOURCES
	config.c
	wrl.c
)

ADD_DEFINITIONS(-Os -Wall -Werror --std=gnu99 -g3 -Wmissing-declarations)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")

ADD_EXECUTABLE(wireless-rate-limiter ${SOURCES})

TARGET_LINK_LIBRARIES(wireless-rate-limiter ubox ubus blobmsg_json ${LIBS_EXTRA})

SET(CMAKE_INSTALL_PREFIX /usr)

INSTALL(TARGETS wireless-rate-limiter
	RUNTIME DESTINATION bin
)
