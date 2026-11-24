cmake_minimum_required(VERSION 3.16)
project(helix-public C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(HELIX_ENGINE_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH
    "Public headers of the Helix engine checkout")
if(NOT EXISTS "${HELIX_ENGINE_INCLUDE}/helix/fs/iasset_plugin.h")
    message(FATAL_ERROR "Helix SDK header not found: ${HELIX_ENGINE_INCLUDE}/helix/fs/iasset_plugin.h")
endif()

# Shipping modules land in the engine build, inside the game they belong to.
# Object files stay in this cmake build directory.
set(HELIX_ENGINE_BUILD "${CMAKE_CURRENT_SOURCE_DIR}/../../build" CACHE PATH
    "Engine build directory that receives shipping modules")

function(hx_deploy_module target game)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${HELIX_ENGINE_BUILD}/${game}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${target}>"
            "${HELIX_ENGINE_BUILD}/${game}/$<TARGET_FILE_NAME:${target}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${target}>"
            "${HELIX_ENGINE_BUILD}/$<TARGET_FILE_NAME:${target}>"
        COMMENT "Deploy ${target} to ${game} and root")
endfunction()

set(HXFS_WAD_SOURCES
    src/wad/wad_file.c
    src/wad/hxfs_wad.c)

add_library(hxfs_wad SHARED ${HXFS_WAD_SOURCES})
target_include_directories(hxfs_wad PRIVATE
    "${HELIX_ENGINE_INCLUDE}"
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
    "${HELIX_ENGINE_INCLUDE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/wad")

set(HXFS_PAK_SOURCES
    src/pak/pak_file.c
    src/pak/hxfs_pak.c)

add_library(hxfs_pak SHARED ${HXFS_PAK_SOURCES})
target_include_directories(hxfs_pak PRIVATE
    "${HELIX_ENGINE_INCLUDE}"
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
    "${HELIX_ENGINE_INCLUDE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/pak")

if(NOT ZLIB_ROOT)
    get_filename_component(_hx_cc_dir "${CMAKE_C_COMPILER}" DIRECTORY)
    get_filename_component(_hx_sysroot "${_hx_cc_dir}" DIRECTORY)
    if(EXISTS "${_hx_sysroot}/include/zlib.h")
        set(ZLIB_ROOT "${_hx_sysroot}")
    endif()
endif()
find_package(ZLIB REQUIRED)
if(WIN32)
    find_library(ZLIB_STATIC_LIB NAMES libz.a zlibstatic PATHS "${_hx_sysroot}/lib")
endif()

set(HXFS_ZIP_CORE src/zip/zip_file.c)

set(HXFS_PK3_SOURCES
    ${HXFS_ZIP_CORE}
    src/pk3/hxfs_pk3.c)

add_library(hxfs_pk3 SHARED ${HXFS_PK3_SOURCES})
target_include_directories(hxfs_pk3 PRIVATE
    "${HELIX_ENGINE_INCLUDE}"
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
    "${HELIX_ENGINE_INCLUDE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/zip"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/pk3")
target_link_libraries(hxfs_pk3_test PRIVATE ZLIB::ZLIB)

set(HXFS_PK4_SOURCES
    ${HXFS_ZIP_CORE}
    src/pk4/hxfs_pk4.c)

add_library(hxfs_pk4 SHARED ${HXFS_PK4_SOURCES})
target_include_directories(hxfs_pk4 PRIVATE
    "${HELIX_ENGINE_INCLUDE}"
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
    "${HELIX_ENGINE_INCLUDE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/zip")
target_link_libraries(hxfs_pk4_test PRIVATE ZLIB::ZLIB)

set(HXFS_IWD_SOURCES
    ${HXFS_ZIP_CORE}
    src/iwd/hxfs_iwd.c)

add_library(hxfs_iwd SHARED ${HXFS_IWD_SOURCES})
target_include_directories(hxfs_iwd PRIVATE
    "${HELIX_ENGINE_INCLUDE}"
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
    "${HELIX_ENGINE_INCLUDE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/zip")
target_link_libraries(hxfs_iwd_test PRIVATE ZLIB::ZLIB)

set(HXFS_COD4_SOURCES
    src/cod4/cod4_iwi_wavelet.c
    ${HXFS_ZIP_CORE}
    src/ff/ff_file.c
    src/cod4/cod4_bsp.c
    src/cod4/cod4_ffmap.c
    src/cod4/hxfs_cod4.c)

add_library(hxfs_cod4 SHARED ${HXFS_COD4_SOURCES})
target_include_directories(hxfs_cod4 PRIVATE
    "${HELIX_ENGINE_INCLUDE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/zip"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ff")
target_link_libraries(hxfs_cod4 PRIVATE ZLIB::ZLIB)
set_target_properties(hxfs_cod4 PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hxfs_cod4"
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
if(WIN32)
    target_compile_definitions(hxfs_cod4 PRIVATE HXFS_COD4_EXPORTS)
endif()
hx_deploy_module(hxfs_cod4 cod4)

add_executable(hxfs_cod4_test
    tests/cod4_plugin_test.c
    ${HXFS_COD4_SOURCES})
target_include_directories(hxfs_cod4_test PRIVATE
    "${HELIX_ENGINE_INCLUDE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/zip"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ff")
target_link_libraries(hxfs_cod4_test PRIVATE ZLIB::ZLIB)
if(NOT WIN32)
    target_link_libraries(hxfs_cod4 PRIVATE m)
    target_link_libraries(hxfs_cod4_test PRIVATE m)
endif()
add_test(NAME hxfs_cod4_test COMMAND hxfs_cod4_test)

set(HXFS_FF_SOURCES
    src/ff/ff_file.c
    src/ff/hxfs_ff.c)

add_library(hxfs_ff SHARED ${HXFS_FF_SOURCES})
target_include_directories(hxfs_ff PRIVATE
    "${HELIX_ENGINE_INCLUDE}"
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
    "${HELIX_ENGINE_INCLUDE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ff")
target_link_libraries(hxfs_ff_test PRIVATE ZLIB::ZLIB)

enable_testing()
add_test(NAME hxfs_wad_test COMMAND hxfs_wad_test)
add_test(NAME hxfs_pak_test COMMAND hxfs_pak_test)
add_test(NAME hxfs_pk3_test COMMAND hxfs_pk3_test)
add_test(NAME hxfs_pk4_test COMMAND hxfs_pk4_test)
add_test(NAME hxfs_iwd_test COMMAND hxfs_iwd_test)
add_test(NAME hxfs_ff_test COMMAND hxfs_ff_test)

# Modular CoD plugin. The game loads hxfs_cod.dll.
set(HXFS_COD_SOURCES
    ${HXFS_ZIP_CORE}
    src/cod/archive/archive.c
    src/cod/bsp/read.c
    src/cod/bsp/names.c
    src/cod/bsp/helix.c
    src/cod/xmodel/read.c
    src/cod/xmodel/helix.c
    src/cod/xanim/read.c
    src/cod/xanim/helix.c
    src/cod/sound/read.c
    src/cod/sound/helix.c
    src/cod/ui/read.c
    src/cod/ui/helix.c
    src/cod/gsc/read.c
    src/cod/gsc/helix.c
    src/cod/gsc/vm.c
    src/cod/gsc/compile.c
    src/cod/gsc/frontend.c
    src/cod/shader/read.c
    src/cod/shader/helix.c
    src/cod/cache.c
    src/cod/bot/cod_nav.c
    src/cod/plugin.c)

add_library(hxfs_cod SHARED ${HXFS_COD_SOURCES})
target_include_directories(hxfs_cod PRIVATE
    "${HELIX_ENGINE_INCLUDE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/zip"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cod")
if(WIN32 AND ZLIB_STATIC_LIB)
    target_link_libraries(hxfs_cod PRIVATE "${ZLIB_STATIC_LIB}")
    target_link_options(hxfs_cod PRIVATE "-static-libgcc")
else()
    target_link_libraries(hxfs_cod PRIVATE ZLIB::ZLIB)
endif()
set_target_properties(hxfs_cod PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hxfs_cod"
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

if(WIN32)
    target_compile_definitions(hxfs_cod PRIVATE HXFS_COD_EXPORTS)
endif()
hx_deploy_module(hxfs_cod hxcod)

add_executable(hxfs_cod_test
    tests/hxfs_cod_test.c
    ${HXFS_COD_SOURCES})
target_include_directories(hxfs_cod_test PRIVATE
    "${HELIX_ENGINE_INCLUDE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/zip"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cod")
target_link_libraries(hxfs_cod_test PRIVATE ZLIB::ZLIB)
add_test(NAME hxfs_cod_test COMMAND hxfs_cod_test)

# CoD GSC server module. The language stays here; the engine only receives the hooks.
add_library(cod_sgame SHARED
    src/cod/anim/playeranim.c
    src/cod/footsteps.c
    src/cod/anim/runtime.c
    src/cod/anim/notes.c
    src/cod/anim/controls.c
    src/cod/gsc/vm.c
    src/cod/gsc/compile.c
    src/cod/gsc/frontend.c
    src/cod/bot/cod_nav.c
    src/cod/bot/zbot.c
    src/cod/sgame.c)
target_include_directories(cod_sgame PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cod"
    "${HELIX_ENGINE_INCLUDE}")

set_target_properties(cod_sgame PROPERTIES
    PREFIX ""
    OUTPUT_NAME "cod_sgame"
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
if(WIN32)
    target_compile_definitions(cod_sgame PRIVATE)
endif()
hx_deploy_module(cod_sgame hxcod)

# Modular True Combat (ET 2.60b) plugin: hxfs_tc.dll
set(HXFS_TC_SOURCES
    ${HXFS_ZIP_CORE}
    src/tc/archive/archive.c
    src/tc/bsp/read.c
    src/tc/bsp/names.c
    src/tc/bsp/helix.c
    src/tc/cache.c
    src/tc/plugin.c)

add_library(hxfs_tc SHARED ${HXFS_TC_SOURCES})
target_include_directories(hxfs_tc PRIVATE
    "${HELIX_ENGINE_INCLUDE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/zip"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/tc")
if(WIN32 AND ZLIB_STATIC_LIB)
    target_link_libraries(hxfs_tc PRIVATE "${ZLIB_STATIC_LIB}")
    target_link_options(hxfs_tc PRIVATE "-static-libgcc")
else()
    target_link_libraries(hxfs_tc PRIVATE ZLIB::ZLIB)
endif()
set_target_properties(hxfs_tc PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hxfs_tc"
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

if(WIN32)
    target_compile_definitions(hxfs_tc PRIVATE HXFS_TC_EXPORTS)
endif()
hx_deploy_module(hxfs_tc hxtc)

add_executable(hxfs_tc_test
    tests/hxfs_tc_test.c
    ${HXFS_TC_SOURCES})
target_include_directories(hxfs_tc_test PRIVATE
    "${HELIX_ENGINE_INCLUDE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/zip"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/tc")
target_link_libraries(hxfs_tc_test PRIVATE ZLIB::ZLIB)
add_test(NAME hxfs_tc_test COMMAND hxfs_tc_test)


# SDK-to-GSC lifecycle and host callback integration.
add_executable(cod_sgame_test tests/cod_sgame_test.c src/cod/bot/zbot.c src/cod/bot/cod_nav.c)

target_include_directories(cod_sgame_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cod"
    "${HELIX_ENGINE_INCLUDE}")
target_link_libraries(cod_sgame_test PRIVATE cod_sgame)

add_test(NAME cod_sgame_test COMMAND cod_sgame_test)
add_test(NAME cod_sgame_linked_test COMMAND cod_sgame_test)
set_tests_properties(cod_sgame_linked_test PROPERTIES ENVIRONMENT "GSC_TEST_RUNTIME=linked")

add_executable(gsc_frontend_test tests/gsc_frontend_test.c
    src/cod/gsc/frontend.c src/cod/gsc/compile.c src/cod/gsc/vm.c)
target_include_directories(gsc_frontend_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/cod")
add_test(NAME gsc_frontend_test COMMAND gsc_frontend_test)

# Opt-in closed-link profile; the shipping cod_sgame retains its current path.
add_executable(gsc_linked_test tests/gsc_linked_test.c
    src/cod/gsc/frontend.c src/cod/gsc/compile.c src/cod/gsc/vm.c)
target_include_directories(gsc_linked_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/cod")
add_test(NAME gsc_linked_test COMMAND gsc_linked_test)
if(UNIX)
    target_link_libraries(gsc_linked_test PRIVATE m)
endif()

if(UNIX)
    target_link_libraries(hxfs_cod PRIVATE m)
    target_link_libraries(hxfs_cod_test PRIVATE m)
    target_link_libraries(cod_sgame PRIVATE m)
    target_link_libraries(cod_sgame_test PRIVATE m)
    target_link_libraries(gsc_frontend_test PRIVATE m)
    target_link_libraries(hxfs_tc PRIVATE m)
    target_link_libraries(hxfs_tc_test PRIVATE m)
endif()


add_executable(cod_anim_test tests/cod_anim_test.c src/cod/anim/playeranim.c src/cod/anim/notes.c src/cod/anim/controls.c)
target_include_directories(cod_anim_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/cod" "${HELIX_ENGINE_INCLUDE}")
add_test(NAME cod_anim_test COMMAND cod_anim_test)

if(UNIX)
    target_link_libraries(cod_anim_test PRIVATE m)
endif()

add_executable(cod4_handshake_test tests/cod4_handshake_test.c src/cod4/cod4_handshake.c)
target_include_directories(cod4_handshake_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/cod4")
add_test(NAME cod4_handshake_test COMMAND cod4_handshake_test)

add_executable(cod_footsteps_test tests/cod_footsteps_test.c src/cod/footsteps.c)
target_include_directories(cod_footsteps_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/cod" "${HELIX_ENGINE_INCLUDE}")
add_test(NAME cod_footsteps_test COMMAND cod_footsteps_test)
if(UNIX)
    target_link_libraries(cod_footsteps_test PRIVATE m)
endif()

# Internal FF conversion regressions use synthetic buffers, without retail assets.
add_executable(cod4_ffmap_regression_test tests/cod4_ffmap_regression_test.c)
target_link_libraries(cod4_ffmap_regression_test PRIVATE ZLIB::ZLIB)
if(NOT WIN32)
    target_link_libraries(cod4_ffmap_regression_test PRIVATE m)
endif()
add_test(NAME cod4_ffmap_regression_test COMMAND cod4_ffmap_regression_test)

add_executable(cod4_iwi_wavelet_test tests/cod4_iwi_wavelet_test.c src/cod4/cod4_iwi_wavelet.c)
target_include_directories(cod4_iwi_wavelet_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/cod4")
add_test(NAME cod4_iwi_wavelet_test COMMAND cod4_iwi_wavelet_test)

# Optional developer probe; retail assets are supplied by the user, never bundled.
add_executable(cod4_gsc_probe tests/cod4_gsc_probe.c ${HXFS_COD4_SOURCES}
    src/cod/gsc/frontend.c src/cod/gsc/compile.c src/cod/gsc/vm.c)
target_include_directories(cod4_gsc_probe PRIVATE ${HELIX_ENGINE_INCLUDE} src/zip src/ff src/cod)
target_link_libraries(cod4_gsc_probe PRIVATE ZLIB::ZLIB)
if(UNIX)
    target_link_libraries(cod4_gsc_probe PRIVATE m)
endif()

# -- Counter-Strike 1.6 (hxfs_cs16) -----------------------------------------
set(HXFS_CS16_SOURCES
    src/cs16/plugin.c
    src/cs16/cs16_bsp.c
    src/cs16/cs16_wad.c
    src/cs16/cs16_mdl.c
    src/wad/wad_file.c)

add_library(hxfs_cs16 SHARED ${HXFS_CS16_SOURCES})
target_include_directories(hxfs_cs16 PRIVATE
    "${HELIX_ENGINE_INCLUDE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cs16"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/wad")
set_target_properties(hxfs_cs16 PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hxfs_cs16"
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
if(UNIX)
    target_link_libraries(hxfs_cs16 PRIVATE m)
endif()
hx_deploy_module(hxfs_cs16 cs16)

add_library(cs16_sgame SHARED
    src/cs16/sgame.c
    src/cod/bot/zbot.c
    src/cod/bot/cod_nav.c)
target_include_directories(cs16_sgame PRIVATE
    "${HELIX_ENGINE_INCLUDE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cod"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cs16")
set_target_properties(cs16_sgame PROPERTIES
    PREFIX ""
    OUTPUT_NAME "cs16_sgame"
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
if(UNIX)
    target_link_libraries(cs16_sgame PRIVATE m)
endif()
hx_deploy_module(cs16_sgame cs16)

add_executable(cs16_mdl_test
    tests/cs16_mdl_test.c
    src/cs16/cs16_mdl.c)
target_include_directories(cs16_mdl_test PRIVATE
    "${HELIX_ENGINE_INCLUDE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cs16")
if(UNIX)
    target_link_libraries(cs16_mdl_test PRIVATE m)
endif()

# -- DOOM + DOOM II (hxfs_doom + doom_sgame) --------------------------------
set(HXFS_DOOM_SOURCES
    src/doom/plugin.c
    src/doom/doom_wad.c
    src/doom/doom_map.c
    src/doom/doom_tex.c)

add_library(hxfs_doom SHARED ${HXFS_DOOM_SOURCES})
target_include_directories(hxfs_doom PRIVATE
    "${HELIX_ENGINE_INCLUDE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/doom")
set_target_properties(hxfs_doom PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hxfs_doom"
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
if(UNIX)
    target_link_libraries(hxfs_doom PRIVATE m)
endif()
hx_deploy_module(hxfs_doom doom)

add_library(doom_sgame SHARED src/doom/sgame.c)
target_include_directories(doom_sgame PRIVATE "${HELIX_ENGINE_INCLUDE}")
set_target_properties(doom_sgame PROPERTIES
    PREFIX ""
    OUTPUT_NAME "doom_sgame"
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
if(UNIX)
    target_link_libraries(doom_sgame PRIVATE m)
endif()
hx_deploy_module(doom_sgame doom)

# -- Half-Life 1 (hl1_sgame) ------------------------------------------------
add_library(hl1_sgame SHARED src/hl1/sgame.c)
target_include_directories(hl1_sgame PRIVATE "${HELIX_ENGINE_INCLUDE}")
set_target_properties(hl1_sgame PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hl1_sgame"
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
if(UNIX)
    target_link_libraries(hl1_sgame PRIVATE m)
endif()
hx_deploy_module(hl1_sgame hl1)
