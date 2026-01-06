CMAKE_MINIMUM_REQUIRED(VERSION 3.13)

SET(CMAKE_CXX_STANDARD 11)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(CMAKE_AUTOMOC ON)

FILE (READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" FILE_VERSION)
STRING (STRIP "${FILE_VERSION}" FILE_VERSION)

PROJECT(DtkLog
  VERSION ${FILE_VERSION}
  DESCRIPTION "Simple, convinient and thread safe logger for Qt-based C++ apps"
  HOMEPAGE_URL "https://github.com/linuxdeepin/dtklog"
  LANGUAGES CXX
)

SET (BUILD_WITH_SYSTEMD OFF CACHE BOOL "Build with systemd")

option(DTK5 "Build DTK5." ON)
if(DTK5)
    set(DTK_VERSION_MAJOR "5")
    set(DTK_NAME_SUFFIX "")
else()
    set(DTK_VERSION_MAJOR "6")
    set(DTK_NAME_SUFFIX "6")
endif()

set(DTK_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(DTK_VERSION_PATCH ${PROJECT_VERSION_PATCH})
# Set so version like 0.x.y, it's required for DTK library ABI compatibility.
set(DTK_VERSION "0.${DTK_VERSION_MINOR}.${DTK_VERSION_PATCH}")
set(QT_VERSION_MAJOR ${DTK_VERSION_MAJOR})

SET (DLog "Dtk${DTK_NAME_SUFFIX}Log")

INCLUDE(GNUInstallDirs)
INCLUDE(CMakePackageConfigHelpers) # configure_package_config_file
SET (INCLUDE_INSTALL_DIR "${CMAKE_INSTALL_INCLUDEDIR}/dtk${DTK_VERSION_MAJOR}/DLog")
SET (LIBRARY_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}")
SET (CONFIG_CMAKE_INSTALL_DIR "${LIBRARY_INSTALL_DIR}/cmake/${DLog}" CACHE STRING "Install dir for cmake config files")
SET (MKSPECS_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/qt${QT_VERSION_MAJOR}/mkspecs/modules" CACHE STRING "Install dir for qt pri files")

FIND_PACKAGE(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)
FIND_PACKAGE(spdlog REQUIRED)

IF(BUILD_WITH_SYSTEMD)
  FIND_PACKAGE(PkgConfig REQUIRED)
  PKG_CHECK_MODULES(Systemd REQUIRED IMPORTED_TARGET libsystemd)
ENDIF()

ADD_DEFINITIONS(-DLIBDLOG_LIBRARY)

INCLUDE_DIRECTORIES(BEFORE include)

SET(sources
  src/Logger.cpp
  src/AbstractAppender.cpp
  src/AbstractStringAppender.cpp
  src/ConsoleAppender.cpp
  src/FileAppender.cpp
  src/RollingFileAppender.cpp
  src/dloghelper.cpp
  src/rollingfilesink_p.h
)

SET(includes
  include/AbstractStringAppender.h
  include/AbstractAppender.h
  include/ConsoleAppender.h
  include/FileAppender.h
  include/RollingFileAppender.h
  include/dloggerdefs.h
  include/dloghelper.h
  include/Logger.h
  include/dlog_global.h
)

# OutputDebugAppender is only for Windows systems
IF(WIN32)
  SET(sources ${sources} src/OutputDebugAppender.cpp)
  SET(includes ${includes} include/OutputDebugAppender.h)
ENDIF(WIN32)

IF(BUILD_WITH_SYSTEMD)
  SET(sources ${sources} src/JournalAppender.cpp)
  SET(includes ${includes} include/JournalAppender.h)
ENDIF(BUILD_WITH_SYSTEMD)

SET(library_target dtk${DTK_NAME_SUFFIX}log)

ADD_LIBRARY(${library_target} SHARED ${sources} ${includes})

TARGET_INCLUDE_DIRECTORIES(${library_target} PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
  "$<INSTALL_INTERFACE:${INCLUDE_INSTALL_DIR}>"
)

TARGET_LINK_DIRECTORIES(${library_target} INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>"
  "$<INSTALL_INTERFACE:${LIBRARY_INSTALL_DIR}>"
)

SET_TARGET_PROPERTIES(${library_target} PROPERTIES
  VERSION ${DTK_VERSION}
  SOVERSION "0"
  EXPORT_NAME Log
)

TARGET_LINK_LIBRARIES(${library_target} PUBLIC Qt${QT_VERSION_MAJOR}::Core)

if("${QT_VERSION_MAJOR}" STREQUAL "6")
  if ("${Qt6Core_VERSION}" VERSION_GREATER_EQUAL 6.10.0)
    find_package(Qt6 REQUIRED COMPONENTS CorePrivate)
  endif()
endif()

TARGET_LINK_LIBRARIES(${library_target} PRIVATE
  Qt${QT_VERSION_MAJOR}::CorePrivate
  spdlog::spdlog
)

IF(BUILD_WITH_SYSTEMD)
  ADD_DEFINITIONS(-DBUILD_WITH_SYSTEMD)
  TARGET_LINK_LIBRARIES(${library_target} PRIVATE PkgConfig::Systemd)
ENDIF()

INSTALL(TARGETS ${library_target}
  EXPORT ${DLog}Targets
  DESTINATION ${LIBRARY_INSTALL_DIR}
)

INSTALL(EXPORT ${DLog}Targets
  FILE ${DLog}Targets.cmake
  NAMESPACE Dtk${DTK_NAME_SUFFIX}::
  DESTINATION ${CONFIG_CMAKE_INSTALL_DIR}
)

INSTALL(FILES ${includes} DESTINATION "${INCLUDE_INSTALL_DIR}")

# pkgconfig files
CONFIGURE_FILE(misc/dtklog.pc.in ${library_target}.pc @ONLY)
INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/${library_target}.pc DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")

# qmake module file
CONFIGURE_FILE(misc/qt_lib_dtklog.pri.in qt_lib_dtklog.pri @ONLY)
INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/qt_lib_dtklog.pri DESTINATION "${MKSPECS_INSTALL_DIR}")

CONFIGURE_PACKAGE_CONFIG_FILE(misc/DLogConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/${DLog}Config.cmake
    INSTALL_DESTINATION ${CONFIG_CMAKE_INSTALL_DIR}
)
WRITE_BASIC_PACKAGE_VERSION_FILE(
    "${CMAKE_CURRENT_BINARY_DIR}/${DLog}ConfigVersion.cmake"
    VERSION ${DTK_VERSION}
    COMPATIBILITY SameMajorVersion
)

INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/${DLog}Config.cmake DESTINATION ${CONFIG_CMAKE_INSTALL_DIR})
INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/${DLog}ConfigVersion.cmake DESTINATION ${CONFIG_CMAKE_INSTALL_DIR})
