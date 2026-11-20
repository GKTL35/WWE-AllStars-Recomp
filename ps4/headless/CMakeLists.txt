# WWE All Stars on PS4, stage 1: the recompiled game on ReXGlue's core + kernel, with NO graphics,
# audio or input. It boots the game's code and logs how far it gets (/data/wwe/headless.log).
#
# Configure (see scripts/ps4-headless-wsl.sh, which does all of this):
#   cmake -S ps4/headless -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=~/ps4-kit/toolchain/ps4-kit-toolchain.cmake \
#         -DPS4_KIT_NETFS=OFF -DREX_SDK_DIR=<patched rexglue-sdk> -DWWE_DIR=<this repo>
cmake_minimum_required(VERSION 3.25)
project(wwe_headless C CXX ASM)

set(PS4_KIT_HOME "$ENV{HOME}/ps4-kit" CACHE PATH "ps4-kit checkout")
include(${PS4_KIT_HOME}/ps4-kit.cmake)

set(REX_SDK_DIR "" CACHE PATH "ReXGlue SDK source tree with patches/rexglue-ps4-platform.patch applied")
set(WWE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "WWE-AllStars-Recomp checkout (with generated/default)")
if(NOT EXISTS "${REX_SDK_DIR}/include/rex/runtime.h")
    message(FATAL_ERROR "REX_SDK_DIR is not a ReXGlue tree: '${REX_SDK_DIR}'")
endif()
if(NOT EXISTS "${WWE_DIR}/generated/default/sources.cmake")
    message(FATAL_ERROR "No ${WWE_DIR}/generated/default/sources.cmake: run codegen first")
endif()
set(R ${REX_SDK_DIR})
set(T ${R}/thirdparty)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

# Flags every SDK and game file gets (the SDK's own: strict FP, no strict aliasing).
add_compile_options(-fno-strict-aliasing -ffp-model=strict -ffp-contract=off -fno-char8_t -O1)
add_compile_definitions(REX_PLATFORM_PS4=1 REXGLUE_BUILD_CONFIG="Release" REX_HAS_VULKAN=0
    SPDLOG_FMT_EXTERNAL=1 SPDLOG_COMPILED_LIB=1 FMT_UNICODE=0)
# Third-party code is not ours to fix.
function(quiet)
    foreach(t ${ARGN})
        target_compile_options(${t} PRIVATE -w)
    endforeach()
endfunction()

# Generated headers (version.h) ---------------------------------------------------------------
set(REXGLUE_BUILD_TIMESTAMP "ps4")
set(REX_PLATFORM "ps4-amd64")
if(EXISTS ${R}/include/rex/version.h.in)
    configure_file(${R}/include/rex/version.h.in ${CMAKE_BINARY_DIR}/include/rex/version.h @ONLY)
endif()
include_directories(${R}/include ${CMAKE_BINARY_DIR}/include ${R}/src)

# Third-party ---------------------------------------------------------------------------------
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${T}/fmt ${CMAKE_BINARY_DIR}/fmt EXCLUDE_FROM_ALL)
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${T}/spdlog ${CMAKE_BINARY_DIR}/spdlog EXCLUDE_FROM_ALL)
quiet(fmt spdlog)

add_library(rex_tp_headers INTERFACE)
target_include_directories(rex_tp_headers INTERFACE
    ${T}/disruptorplus/include ${T}/tomlplusplus/include ${T}/simde ${T}/simde/simde
    ${T}/utfcpp/source ${T}/cli11/include ${T}/imgui ${T}/xxHash ${T}/spirv-headers/include
    ${T}/vulkan-headers/include ${T}/sdl3/include ${T}/o1heap/o1heap ${T}/aes_128 ${T}
    ${T}/libmspack/cabextract/mspack ${T}/renderdoc)
target_link_libraries(rex_tp_headers INTERFACE fmt::fmt spdlog::spdlog)
target_compile_definitions(rex_tp_headers INTERFACE XXH_INLINE_ALL=1)

add_library(rex_tp STATIC
    ${T}/aes_128/aes.c ${T}/libmspack/cabextract/mspack/lzxd.c ${T}/o1heap/o1heap/o1heap.c
    ${T}/imgui/imgui.cpp ${T}/imgui/imgui_draw.cpp ${T}/imgui/imgui_tables.cpp ${T}/imgui/imgui_widgets.cpp)
target_link_libraries(rex_tp PUBLIC rex_tp_headers)
target_compile_definitions(rex_tp PRIVATE "O1HEAP_CONFIG_HEADER=\"${R}/include/rex/o1heap_config.h\"")
quiet(rex_tp)

# ReXGlue, the headless subset -----------------------------------------------------------------
function(rex_srcs out dir)
    set(l)
    foreach(f ${ARGN})
        list(APPEND l ${R}/src/${dir}/${f})
    endforeach()
    set(${out} ${l} PARENT_SCOPE)
endfunction()

rex_srcs(CORE core
    arena.cpp bit_map.cpp bit_stream.cpp byte_stream.cpp clock.cpp cvar.cpp exception_handler.cpp
    filesystem.cpp filesystem_wildcard.cpp hash.cpp logging.cpp memory.cpp mutex.cpp ring_buffer.cpp
    string_buffer.cpp threading.cpp timer_queue.cpp utf8.cpp vec128.cpp
    atomic_posix.cpp clock_posix.cpp dbg_posix.cpp dynlib_posix.cpp exception_handler_posix.cpp
    filesystem_posix.cpp fiber_ps4.cpp mapped_memory_posix.cpp math_gcc.cpp memory_ps4.cpp
    platform/console_posix.cpp platform/env_posix.cpp seh_posix.cpp socket_posix.cpp system_posix.cpp
    threading_posix.cpp)
list(APPEND CORE
    ${R}/src/graphics/xenos.cpp
    ${R}/src/graphics/pipeline/texture/info.cpp ${R}/src/graphics/pipeline/texture/info_formats.cpp
    ${R}/src/graphics/pipeline/texture/extent.cpp ${R}/src/graphics/pipeline/texture/util.cpp
    ${R}/src/graphics/pipeline/texture/conversion.cpp)
rex_srcs(FS filesystem
    device.cpp entry.cpp virtual_file_system.cpp devices/disc_image_device.cpp devices/disc_image_entry.cpp
    devices/disc_image_file.cpp devices/host_path_device.cpp devices/host_path_entry.cpp
    devices/host_path_file.cpp devices/null_device.cpp devices/null_entry.cpp devices/null_file.cpp
    devices/stfs_container_device.cpp devices/stfs_container_entry.cpp devices/stfs_container_file.cpp)
rex_srcs(SYS system
    runtime.cpp embedded_metadata.cpp achievement_manager.cpp achievements.cpp kernel_state.cpp xmemory.cpp
    kernel_module.cpp user_module.cpp xthread.cpp xevent.cpp xfile.cpp xmutant.cpp xsemaphore.cpp xtimer.cpp
    xiocompletion.cpp xnotifylistener.cpp xenumerator.cpp xsocket.cpp xsymboliclink.cpp xobject.cpp
    xmodule.cpp guest_path.cpp util/object_table.cpp util/native_list.cpp util/xdbf_utils.cpp
    util/gameinfo_utils.cpp xam/app_manager.cpp xam/content_manager.cpp xam/user_profile.cpp ppc_types.cpp
    elf_module.cpp entry_table.cpp export_resolver.cpp lzx.cpp mmio_handler.cpp map_parser.cpp module.cpp
    function_dispatcher.cpp gpu_plugin_loader.cpp thread.cpp thread_state.cpp xex_module.cpp)
rex_srcs(KRN kernel
    kernel_init.cpp xam/xam_module.cpp xam/xam_avatar.cpp xam/xam_content.cpp xam/xam_content_aggregate.cpp
    xam/xam_content_device.cpp xam/xam_debug.cpp xam/xam_enum.cpp xam/xam_info.cpp xam/xam_input.cpp
    xam/xam_locale.cpp xam/xam_msg.cpp xam/xam_net.cpp xam/xam_notify.cpp xam/xam_nui.cpp xam/xam_party.cpp
    xam/xam_task.cpp xam/xam_ui.cpp xam/xam_user.cpp xam/xam_video.cpp xam/xam_voice.cpp xam/xam_misc.cpp
    xam/apps/xam_app.cpp xam/apps/xgi_app.cpp xam/apps/xlivebase_app.cpp xam/apps/xmp_app.cpp
    xbdm/xbdm_module.cpp xbdm/xbdm_misc.cpp xboxkrnl/xboxkrnl_crypt.cpp xboxkrnl/xboxkrnl_debug.cpp
    xboxkrnl/xboxkrnl_error.cpp xboxkrnl/xboxkrnl_hal.cpp xboxkrnl/xboxkrnl_hid.cpp xboxkrnl/xboxkrnl_io.cpp
    xboxkrnl/xboxkrnl_io_info.cpp xboxkrnl/xboxkrnl_memory.cpp xboxkrnl/xboxkrnl_misc.cpp
    xboxkrnl/xboxkrnl_module.cpp xboxkrnl/xboxkrnl_modules.cpp xboxkrnl/xboxkrnl_ob.cpp
    xboxkrnl/xboxkrnl_rtl.cpp xboxkrnl/xboxkrnl_strings.cpp xboxkrnl/xboxkrnl_threading.cpp
    xboxkrnl/xboxkrnl_video.cpp xboxkrnl/xboxkrnl_xconfig.cpp xboxkrnl/xboxkrnl_audio.cpp
    xboxkrnl/xboxkrnl_audio_xma.cpp crt/heap.cpp crt/file.cpp crt/memory.cpp crt/string.cpp crt/threading.cpp)
# UI: only what the kernel references (dialogs, window and app-context types); no SDL, Vulkan or overlays.
rex_srcs(UI ui windowed_app_context.cpp window.cpp imgui_dialog.cpp imgui_drawer.cpp immediate_drawer.cpp)
rex_srcs(AUD audio audio_driver.cpp audio_system.cpp downmix.cpp xma_register_file.cpp nop/nop_audio_system.cpp)
rex_srcs(INP input input_system.cpp device_assignment.cpp state_merge.cpp nop/nop_input_driver.cpp)

# Files that fail to build the first time are listed in the optional skip list so the rest still link
# (stubs for them go in stubs.cpp). Empty by default.
set(REX_SKIP "" CACHE STRING "SDK source files (basename) to leave out")
set(ALL_REX ${CORE} ${FS} ${SYS} ${KRN} ${UI} ${AUD} ${INP})
foreach(s ${REX_SKIP})
    list(FILTER ALL_REX EXCLUDE REGEX "/${s}$")
endforeach()
add_library(rexps4 STATIC ${ALL_REX})
target_include_directories(rexps4 PUBLIC ${T}/fmt/include ${T}/spdlog/include)
target_link_libraries(rexps4 PUBLIC rex_tp)
target_compile_options(rexps4 PRIVATE -Wno-error)

# The recompiled game --------------------------------------------------------------------------
include(${WWE_DIR}/generated/default/sources.cmake)
set_source_files_properties(${GENERATED_SOURCES} PROPERTIES GENERATED FALSE)
add_library(wwe_recomp OBJECT ${GENERATED_SOURCES})
target_include_directories(wwe_recomp PRIVATE ${WWE_DIR} ${WWE_DIR}/src ${WWE_DIR}/generated/default)
target_link_libraries(wwe_recomp PRIVATE rexps4)
target_compile_options(wwe_recomp PRIVATE -w -g0)

# The program ----------------------------------------------------------------------------------
add_executable(wwe_headless main.cpp stubs.cpp)
target_include_directories(wwe_headless PRIVATE ${WWE_DIR} ${WWE_DIR}/src ${WWE_DIR}/generated/default)
target_link_libraries(wwe_headless PRIVATE wwe_recomp rexps4)
target_link_options(wwe_headless PRIVATE -Wl,--error-limit=0)
ps4_kit_app(wwe_headless)
