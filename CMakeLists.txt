cmake_minimum_required(VERSION 3.16)
project(helix-public C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(HXFS_WAD_SOURCES
    src/wad/wad_file.c
    src/wad/hxfs_wad.c)

add_library(hxfs_wad SHARED ${HXFS_WAD_SOURCES})
target_include_directories(hxfs_wad PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/helix-sdk"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/wad")
set_target_properties(hxfs_wad PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hxfs_wad"
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

if(WIN32)
    target_compile_definitions(hxfs_wad PRIVATE HXFS_WAD_EXPORTS)
endif()

add_executable(hxfs_wad_test
    tests/wad_test.c
    ${HXFS_WAD_SOURCES})
target_include_directories(hxfs_wad_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/helix-sdk"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/wad")

enable_testing()
add_test(NAME hxfs_wad_test COMMAND hxfs_wad_test)
