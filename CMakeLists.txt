CMAKE_MINIMUM_REQUIRED(VERSION 3.1)

SET(CMAKE_CXX_STANDARD 11)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)

set (CUTE_LOGGER_VERSION "0.0.1" CACHE STRING "define project version")

PROJECT(CuteLogger
 VERSION ${CUTE_LOGGER_VERSION}
 DESCRIPTION "Simple, convinient and thread safe logger for Qt-based C++ apps"
  LANGUAGES CXX
)

FIND_PACKAGE(Qt5Core REQUIRED)

ADD_DEFINITIONS(-DCUTELOGGER_LIBRARY)

INCLUDE_DIRECTORIES(BEFORE include)

SET(sources
  src/Logger.cpp
  src/AbstractAppender.cpp
  src/AbstractStringAppender.cpp
  src/ConsoleAppender.cpp
  src/FileAppender.cpp
  src/RollingFileAppender.cpp
)

SET(includes
  include/Logger.h
  include/FileAppender.h
  include/CuteLogger_global.h
  include/ConsoleAppender.h
  include/AbstractStringAppender.h
  include/AbstractAppender.h
  include/RollingFileAppender.h
)

include(GNUInstallDirs)
SET (INCLUDE_INSTALL_DIR "${CMAKE_INSTALL_INCLUDEDIR}/CuteLogger")
SET (LIBRARY_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}")

# OutputDebugAppender is only for Windows systems
IF(WIN32)
  SET(sources ${sources} src/OutputDebugAppender.cpp)
  SET(includes ${includes} include/OutputDebugAppender.h)
ENDIF(WIN32)

SET(library_target CuteLogger)

ADD_LIBRARY(${library_target} SHARED ${sources} ${includes})

set_target_properties(${library_target} PROPERTIES
  VERSION ${CMAKE_PROJECT_VERSION}
  SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
  EXPORT_NAME Core
)

TARGET_LINK_LIBRARIES(${library_target} Qt5::Core)
TARGET_INCLUDE_DIRECTORIES(${library_target} PUBLIC include)

INSTALL(TARGETS ${library_target} DESTINATION  ${CONFIG_CMAKE_INSTALL_DIR})

INSTALL(FILES ${includes} DESTINATION "${INCLUDE_INSTALL_DIR}")
configure_file(misc/CuteLogger.pc.in CuteLogger.pc @ONLY)
INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/CuteLogger.pc DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")

SET(ENABLE_TESTS OFF CACHE BOOL "Enable building CuteLogger tests")
IF (ENABLE_TESTS)
  SET(CMAKE_AUTOMOC ON)
  FIND_PACKAGE(Qt5Test REQUIRED)

  ADD_EXECUTABLE(basictest test/basictest.cpp)
  TARGET_LINK_LIBRARIES(basictest Qt5::Core Qt5::Test CuteLogger)
ENDIF ()
