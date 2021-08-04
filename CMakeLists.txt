cmake_minimum_required(VERSION 3.10)
project(findstr)
set(CMAKE_CXX_STANDARD 17)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(LINUX TRUE)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(DARWIN TRUE)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
    set(FREEBSD TRUE)
endif()

# checking if we are called in the correct way:
#  with a -B argument.  and without a cache file in the source directory.
if (CMAKE_CACHEFILE_DIR STREQUAL "${CMAKE_SOURCE_DIR}")
    message(FATAL_ERROR "\nUnexpected CMakeCache.txt file in the source directory. Please remove it.")
    return()
endif()

if (EXISTS ${CMAKE_BINARY_DIR}/CMakeLists.txt)
    message(FATAL_ERROR "\nRun cmake with an explicit -B buildpath")
    return()
endif()

if(MSVC)
    # /MP = multithreaded build
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
    # /utf-8 = utf8 source and execution
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
endif()

if (WIN32)
    add_definitions(-DNOMINMAX -DNOGDI)
endif()

file(GLOB CPPUTILS_DIRS cpputils  submodules/cpputils ../cpputils ../../cpputils)
find_path(CPPUTILS_PATH NAMES string-lineenum.h PATHS ${CPPUTILS_DIRS})
if(NOT CPPUTILS_PATH)
    include(FetchContent)
    FetchContent_Populate(cpputils
        GIT_REPOSITORY https://github.com/nlitsme/cpputils)
    set(CPPUTILS_PATH ${CMAKE_BINARY_DIR}/cpputils-src)
endif()
add_library(cpputils INTERFACE)
target_include_directories(cpputils INTERFACE ${CPPUTILS_PATH})

file(GLOB HEXDUMPER_DIRS hexdumper  submodules/hexdumper ../hexdumper ../../hexdumper)
find_path(HEXDUMPER_PATH NAMES string-lineenum.h PATHS ${HEXDUMPER_DIRS})
if(NOT HEXDUMPER_PATH)
    include(FetchContent)
    FetchContent_Populate(hexdumper
        GIT_REPOSITORY https://github.com/nlitsme/hexdumper)
    set(HEXDUMPER_PATH ${CMAKE_BINARY_DIR}/hexdumper-src)
endif()
add_library(hexdumper INTERFACE)
target_include_directories(hexdumper INTERFACE ${HEXDUMPER_PATH})


find_package(Boost REQUIRED COMPONENTS regex)

add_executable(findstr ${CMAKE_SOURCE_DIR}/findstr.cpp)
target_compile_definitions(findstr PUBLIC USE_BOOST_REGEX)
target_link_libraries(findstr Boost::headers Boost::regex)
target_link_libraries(findstr cpputils)
if (DARWIN)
	target_link_libraries(findstr hexdumper)
endif()

