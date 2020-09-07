cmake_minimum_required(VERSION 3.2)

cmake_policy(SET CMP0063 NEW)

project(test VERSION 0.0.1)

configure_file(version.h.in ${CMAKE_BINARY_DIR}/version.h)
include_directories(${CMAKE_BINARY_DIR})

if(NOT EXISTS "${CMAKE_BINARY_DIR}/conan.cmake")
   message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
   file(DOWNLOAD "https://github.com/conan-io/cmake-conan/raw/v0.14/conan.cmake"
                 "${CMAKE_BINARY_DIR}/conan.cmake")
endif()

include(${CMAKE_BINARY_DIR}/conan.cmake)

conan_cmake_run(REQUIRES
                    boost/1.70.0@conan/stable
                OPTIONS
                    boost:layout=system
                BASIC_SETUP CMAKE_TARGETS
                BUILD missing)

add_subdirectory(src)
