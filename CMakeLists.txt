cmake_minimum_required(VERSION 3.10)
project(findstr)
set(CMAKE_CXX_STANDARD 17)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_find")

include(boilerplate)

find_package(cpputils REQUIRED)
find_package(hexdumper REQUIRED)

find_package(Boost REQUIRED COMPONENTS regex)

add_executable(findstr ${CMAKE_SOURCE_DIR}/findstr.cpp)
target_compile_definitions(findstr PUBLIC USE_BOOST_REGEX)
target_link_libraries(findstr Boost::headers Boost::regex)
target_link_libraries(findstr cpputils)
if (DARWIN)
	target_link_libraries(findstr hexdumper)
endif()

