# (Project root CMakeLists.txt)

cmake_minimum_required(VERSION 3.16)

# Copy component configuration files to project root
# This allows the build system to find them without requiring users to move them
set(COMPONENT_DIR "${CMAKE_SOURCE_DIR}/components/a2dpSinkHfpClient")

# Copy sdkconfig.defaults from component to project root if it doesn't exist
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/sdkconfig.defaults")
    if(EXISTS "${COMPONENT_DIR}/sdkconfig.defaults")
        file(COPY "${COMPONENT_DIR}/sdkconfig.defaults" DESTINATION "${CMAKE_SOURCE_DIR}")
        message(STATUS "Copied sdkconfig.defaults from component")
    endif()
endif()

# Copy partitions.csv from component to project root if it doesn't exist
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/partitions.csv")
    if(EXISTS "${COMPONENT_DIR}/partitions.csv")
        file(COPY "${COMPONENT_DIR}/partitions.csv" DESTINATION "${CMAKE_SOURCE_DIR}")
        message(STATUS "Copied partitions.csv from component")
    endif()
endif()

# Include the rest of your project configuration
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(a2dpSinkHfpClient)
