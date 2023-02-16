if (TARGET hexdumper)
    return()
endif()
find_path(HEXDUMPER_DIR NAMES dump_ossl_hash.h PATHS symlinks/hexdumper)
if(HEXDUMPER_DIR STREQUAL "HEXDUMPER_DIR-NOTFOUND")
    include(FetchContent)
    FetchContent_Populate(hexdumper
        GIT_REPOSITORY https://github.com/nlitsme/hexdumper)
    set(HEXDUMPER_DIR ${hexdumper_SOURCE_DIR})
else()
    set(hexdumper_BINARY_DIR ${CMAKE_BINARY_DIR}/hexdumper-build)
endif()

add_library(hexdumper INTERFACE)
target_include_directories(hexdumper INTERFACE ${HEXDUMPER_DIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(hexdumper REQUIRED_VARS HEXDUMPER_DIR)

