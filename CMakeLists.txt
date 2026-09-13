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

set(HXFS_PAK_SOURCES
    src/pak/pak_file.c
    src/pak/hxfs_pak.c)

add_library(hxfs_pak SHARED ${HXFS_PAK_SOURCES})
target_include_directories(hxfs_pak PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/helix-sdk"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/pak")
set_target_properties(hxfs_pak PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hxfs_pak"
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

if(WIN32)
    target_compile_definitions(hxfs_pak PRIVATE HXFS_PAK_EXPORTS)
endif()

add_executable(hxfs_pak_test
    tests/pak_test.c
    ${HXFS_PAK_SOURCES})
target_include_directories(hxfs_pak_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/helix-sdk"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/pak")

if(NOT ZLIB_ROOT)
    get_filename_component(_hx_cc_dir "${CMAKE_C_COMPILER}" DIRECTORY)
    get_filename_component(_hx_sysroot "${_hx_cc_dir}" DIRECTORY)
    if(EXISTS "${_hx_sysroot}/include/zlib.h")
        set(ZLIB_ROOT "${_hx_sysroot}")
    endif()
endif()
find_package(ZLIB REQUIRED)

set(HXFS_ZIP_CORE src/zip/zip_file.c)

set(HXFS_PK3_SOURCES
    ${HXFS_ZIP_CORE}
    src/pk3/hxfs_pk3.c)

add_library(hxfs_pk3 SHARED ${HXFS_PK3_SOURCES})
target_include_directories(hxfs_pk3 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/helix-sdk"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/zip")
target_link_libraries(hxfs_pk3 PRIVATE ZLIB::ZLIB)
set_target_properties(hxfs_pk3 PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hxfs_pk3"
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

if(WIN32)
    target_compile_definitions(hxfs_pk3 PRIVATE HXFS_PK3_EXPORTS)
endif()

add_executable(hxfs_pk3_test
    tests/pk3_test.c
    ${HXFS_PK3_SOURCES})
target_include_directories(hxfs_pk3_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/helix-sdk"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/zip"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/pk3")
target_link_libraries(hxfs_pk3_test PRIVATE ZLIB::ZLIB)

set(HXFS_PK4_SOURCES
    ${HXFS_ZIP_CORE}
    src/pk4/hxfs_pk4.c)

add_library(hxfs_pk4 SHARED ${HXFS_PK4_SOURCES})
target_include_directories(hxfs_pk4 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/helix-sdk"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/zip")
target_link_libraries(hxfs_pk4 PRIVATE ZLIB::ZLIB)
set_target_properties(hxfs_pk4 PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hxfs_pk4"
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

if(WIN32)
    target_compile_definitions(hxfs_pk4 PRIVATE HXFS_PK4_EXPORTS)
endif()

add_executable(hxfs_pk4_test
    tests/pk4_test.c
    ${HXFS_PK4_SOURCES})
target_include_directories(hxfs_pk4_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/helix-sdk"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/zip")
target_link_libraries(hxfs_pk4_test PRIVATE ZLIB::ZLIB)

set(HXFS_IWD_SOURCES
    ${HXFS_ZIP_CORE}
    src/iwd/hxfs_iwd.c)

add_library(hxfs_iwd SHARED ${HXFS_IWD_SOURCES})
target_include_directories(hxfs_iwd PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/helix-sdk"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/zip")
target_link_libraries(hxfs_iwd PRIVATE ZLIB::ZLIB)
set_target_properties(hxfs_iwd PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hxfs_iwd"
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

if(WIN32)
    target_compile_definitions(hxfs_iwd PRIVATE HXFS_IWD_EXPORTS)
endif()

add_executable(hxfs_iwd_test
    tests/iwd_test.c
    ${HXFS_IWD_SOURCES})
target_include_directories(hxfs_iwd_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/helix-sdk"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/zip")
target_link_libraries(hxfs_iwd_test PRIVATE ZLIB::ZLIB)

set(HXFS_FF_SOURCES
    src/ff/ff_file.c
    src/ff/hxfs_ff.c)

add_library(hxfs_ff SHARED ${HXFS_FF_SOURCES})
target_include_directories(hxfs_ff PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/helix-sdk"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ff")
target_link_libraries(hxfs_ff PRIVATE ZLIB::ZLIB)
set_target_properties(hxfs_ff PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hxfs_ff"
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

if(WIN32)
    target_compile_definitions(hxfs_ff PRIVATE HXFS_FF_EXPORTS)
endif()

add_executable(hxfs_ff_test
    tests/ff_test.c
    ${HXFS_FF_SOURCES})
target_include_directories(hxfs_ff_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/helix-sdk"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ff")
target_link_libraries(hxfs_ff_test PRIVATE ZLIB::ZLIB)

enable_testing()
add_test(NAME hxfs_wad_test COMMAND hxfs_wad_test)
add_test(NAME hxfs_pak_test COMMAND hxfs_pak_test)
add_test(NAME hxfs_pk3_test COMMAND hxfs_pk3_test)
add_test(NAME hxfs_pk4_test COMMAND hxfs_pk4_test)
add_test(NAME hxfs_iwd_test COMMAND hxfs_iwd_test)
add_test(NAME hxfs_ff_test COMMAND hxfs_ff_test)
