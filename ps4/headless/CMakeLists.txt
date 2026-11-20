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
option(WWE_GRAPHICS "Build the graphics version (allstars) on top of the headless pieces" ON)
if(WWE_GRAPHICS)
    set(WWE_VK 1)
else()
    set(WWE_VK 0)
endif()
add_compile_definitions(REX_PLATFORM_PS4=1 REXGLUE_BUILD_CONFIG="Release"
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
    ${T}/libmspack/cabextract/mspack ${T}/renderdoc
    ${T}/vulkan-memory-allocator/include ${T}/spirv-tools/include ${T}/glslang ${T}/stb)
target_link_libraries(rex_tp_headers INTERFACE fmt::fmt spdlog::spdlog)
target_compile_definitions(rex_tp_headers INTERFACE XXH_INLINE_ALL=1)

add_library(rex_tp STATIC
    ${T}/aes_128/aes.c ${T}/libmspack/cabextract/mspack/lzxd.c ${T}/o1heap/o1heap/o1heap.c
    ${T}/imgui/imgui.cpp ${T}/imgui/imgui_draw.cpp ${T}/imgui/imgui_tables.cpp ${T}/imgui/imgui_widgets.cpp)
target_link_libraries(rex_tp PUBLIC rex_tp_headers)
target_compile_definitions(rex_tp PRIVATE "O1HEAP_CONFIG_HEADER=\"${R}/include/rex/o1heap_config.h\"")
quiet(rex_tp)


# FFmpeg (libavutil + libavcodec, the XMA/WMA-Pro decoder): the SDK's own trimmed build, taken from its
# thirdparty/CMakeLists.txt so the file lists stay identical to the SDK's.
set(IS_X86_64 TRUE)
set(IS_ARM64 FALSE)
file(READ ${T}/CMakeLists.txt _sdk_tp)
string(FIND "${_sdk_tp}" "set(FFMPEG_DIR" _ff_begin)
string(FIND "${_sdk_tp}" "FOLDER \"FFmpeg\")" _ff_end)
if(_ff_begin EQUAL -1 OR _ff_end EQUAL -1)
    message(FATAL_ERROR "Could not find the FFmpeg block in the SDK's thirdparty/CMakeLists.txt")
endif()
math(EXPR _ff_len "${_ff_end} - ${_ff_begin} + 40")
string(SUBSTRING "${_sdk_tp}" ${_ff_begin} ${_ff_len} _ff_block)
string(REPLACE "\${CMAKE_CURRENT_SOURCE_DIR}" "${T}" _ff_block "${_ff_block}")
file(WRITE ${CMAKE_BINARY_DIR}/ffmpeg_block.cmake "${_ff_block}")
include(${CMAKE_BINARY_DIR}/ffmpeg_block.cmake)
# FFmpeg's config.h has no PS4 branch: ours (ffmpeg-ps4/config.h) goes first on the include path.
target_include_directories(libavutil BEFORE PRIVATE ${CMAKE_CURRENT_LIST_DIR}/ffmpeg-ps4)
target_include_directories(libavcodec BEFORE PRIVATE ${CMAKE_CURRENT_LIST_DIR}/ffmpeg-ps4)

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
rex_srcs(UI ui windowed_app_context.cpp window.cpp imgui_dialog.cpp imgui_drawer.cpp immediate_drawer.cpp presenter.cpp)
if(WWE_GRAPHICS)
    # The SDK's Vulkan Xenos GPU, its presenter, and the Vulkan UI layer (no SDL, no overlays).
    rex_srcs(GFX graphics
        flags.cpp register_file.cpp registers.cpp pipeline/shader/shader.cpp format/ucode.cpp sampler_info.cpp
        graphics_system.cpp command_processor.cpp util/draw_extent_estimator.cpp util/draw.cpp
        packet_disassembler.cpp primitive_processor.cpp pipeline/render_target/cache.cpp shared_memory.cpp
        pipeline/texture/cache.cpp pipeline/shader/interpreter.cpp pipeline/shader/translator.cpp
        pipeline/shader/translator_disasm.cpp
        pipeline/shader/spirv_builder.cpp pipeline/shader/spirv.cpp pipeline/shader/spirv_translator.cpp
        pipeline/shader/spirv_translator_alu.cpp pipeline/shader/spirv_translator_fetch.cpp
        pipeline/shader/spirv_translator_memexport.cpp pipeline/shader/spirv_translator_rb.cpp
        vulkan/graphics_system.cpp vulkan/deferred_command_buffer.cpp vulkan/command_processor.cpp
        vulkan/pipeline_cache.cpp vulkan/primitive_processor.cpp vulkan/render_target_cache.cpp
        vulkan/shader.cpp vulkan/shared_memory.cpp vulkan/texture_cache.cpp)
    rex_srcs(UIVK ui
        graphics_upload_buffer_pool.cpp graphics_util.cpp image_decode.cpp renderdoc_api.cpp menu_item.cpp
        vulkan/linked_type_descriptor_set_allocator.cpp vulkan/single_layout_descriptor_set_pool.cpp
        vulkan/spirv_tools_context.cpp vulkan/ui_samplers.cpp vulkan/vulkan_device.cpp
        vulkan/vulkan_immediate_drawer.cpp vulkan/vulkan_instance.cpp vulkan/vulkan_mem_alloc.cpp
        vulkan/vulkan_presenter.cpp vulkan/vulkan_provider.cpp vulkan/vulkan_submission_tracker.cpp
        vulkan/vulkan_upload_buffer_pool.cpp vulkan/vulkan_util.cpp)
    # glslang: the shader translator emits through its SPIR-V builder, and the command processor compiles
    # GLSL (gamma, FXAA, tessellation helpers) at runtime. Built straight from its sources (its own CMake
    # picks the OS from CMAKE_SYSTEM_NAME), without spirv-opt (ENABLE_OPT=0), as the SDK builds it.
    set(GL ${T}/glslang)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/glslang_gen/glslang)
    file(WRITE ${CMAKE_BINARY_DIR}/glslang_gen/glslang/build_info.h
"#ifndef GLSLANG_BUILD_INFO
#define GLSLANG_BUILD_INFO
#define GLSLANG_VERSION_MAJOR 15
#define GLSLANG_VERSION_MINOR 0
#define GLSLANG_VERSION_PATCH 0
#define GLSLANG_VERSION_FLAVOR \"\"
#define GLSLANG_VERSION_GREATER_THAN(major, minor, patch) (((major) > GLSLANG_VERSION_MAJOR) || ((major) == GLSLANG_VERSION_MAJOR && (((minor) > GLSLANG_VERSION_MINOR) || ((minor) == GLSLANG_VERSION_MINOR && ((patch) > GLSLANG_VERSION_PATCH)))))
#define GLSLANG_VERSION_GREATER_OR_EQUAL_TO(major, minor, patch) (((major) > GLSLANG_VERSION_MAJOR) || ((major) == GLSLANG_VERSION_MAJOR && (((minor) > GLSLANG_VERSION_MINOR) || ((minor) == GLSLANG_VERSION_MINOR && ((patch) >= GLSLANG_VERSION_PATCH)))))
#define GLSLANG_VERSION_LESS_THAN(major, minor, patch) (((major) < GLSLANG_VERSION_MAJOR) || ((major) == GLSLANG_VERSION_MAJOR && (((minor) < GLSLANG_VERSION_MINOR) || ((minor) == GLSLANG_VERSION_MINOR && ((patch) < GLSLANG_VERSION_PATCH)))))
#define GLSLANG_VERSION_LESS_OR_EQUAL_TO(major, minor, patch) (((major) < GLSLANG_VERSION_MAJOR) || ((major) == GLSLANG_VERSION_MAJOR && (((minor) < GLSLANG_VERSION_MINOR) || ((minor) == GLSLANG_VERSION_MINOR && ((patch) <= GLSLANG_VERSION_PATCH)))))
#endif
")
    file(GLOB GL_MI ${GL}/glslang/MachineIndependent/*.cpp ${GL}/glslang/MachineIndependent/preprocessor/*.cpp)
    file(GLOB GL_GCG ${GL}/glslang/GenericCodeGen/*.cpp)
    add_library(rex_glslang STATIC ${GL_MI} ${GL_GCG} ${GL}/OGLCompilersDLL/InitializeDll.cpp
        ${CMAKE_CURRENT_LIST_DIR}/glslang_ps4_os.cpp
        ${GL}/SPIRV/GlslangToSpv.cpp ${GL}/SPIRV/SpvBuilder.cpp ${GL}/SPIRV/SpvPostProcess.cpp
        ${GL}/SPIRV/InReadableOrder.cpp ${GL}/SPIRV/Logger.cpp ${GL}/SPIRV/doc.cpp ${GL}/SPIRV/disassemble.cpp
        ${GL}/SPIRV/SpvTools.cpp)
    target_include_directories(rex_glslang PUBLIC ${GL} ${CMAKE_BINARY_DIR}/glslang_gen ${T}/spirv-headers/include)
    target_compile_definitions(rex_glslang PUBLIC ENABLE_OPT=0 GLSLANG_OSINCLUDE_UNIX)
    quiet(rex_glslang)
    list(APPEND GFX_EXTRA_LINK rex_glslang)
endif()
rex_srcs(AUD audio audio_driver.cpp audio_system.cpp downmix.cpp xma_register_file.cpp xma_context.cpp
    xma_decoder.cpp nop/nop_audio_system.cpp)
rex_srcs(INP input input_system.cpp device_assignment.cpp state_merge.cpp nop/nop_input_driver.cpp)

# Files that fail to build the first time are listed in the optional skip list so the rest still link
# (stubs for them go in stubs.cpp). Empty by default.
set(REX_SKIP "" CACHE STRING "SDK source files (basename) to leave out")
set(ALL_REX ${CORE} ${FS} ${SYS} ${KRN} ${UI} ${AUD} ${INP} ${GFX} ${UIVK})
foreach(s ${REX_SKIP})
    list(FILTER ALL_REX EXCLUDE REGEX "/${s}$")
endforeach()
add_library(rexps4 STATIC ${ALL_REX})
target_include_directories(rexps4 PUBLIC ${T}/fmt/include ${T}/spdlog/include)
target_link_libraries(rexps4 PUBLIC rex_tp libavcodec libavutil ${GFX_EXTRA_LINK})
# Only the engine and the graphics program see REX_HAS_VULKAN: the generated game code stays as it was built.
target_compile_definitions(rexps4 PRIVATE REX_HAS_VULKAN=${WWE_VK})
target_compile_options(rexps4 PRIVATE -Wno-error)

# The recompiled game --------------------------------------------------------------------------
include(${WWE_DIR}/generated/default/sources.cmake)
set_source_files_properties(${GENERATED_SOURCES} PROPERTIES GENERATED FALSE)
add_library(wwe_recomp OBJECT ${GENERATED_SOURCES})
target_include_directories(wwe_recomp PRIVATE ${WWE_DIR} ${WWE_DIR}/src ${WWE_DIR}/generated/default)
target_link_libraries(wwe_recomp PRIVATE rexps4)
target_compile_options(wwe_recomp PRIVATE -w -g0)

# The program ----------------------------------------------------------------------------------
set(WWE_AUTO_STUBS "" CACHE FILEPATH "Generated stand-ins for the game's hook functions (scripts do this)")
add_executable(wwe_headless main.cpp stubs.cpp ${WWE_AUTO_STUBS})
target_include_directories(wwe_headless PRIVATE ${WWE_DIR} ${WWE_DIR}/src ${WWE_DIR}/generated/default)
target_link_libraries(wwe_headless PRIVATE wwe_recomp rexps4)
target_link_options(wwe_headless PRIVATE --error-limit=0)
ps4_kit_app(wwe_headless)

if(WWE_GRAPHICS)
    # The game with graphics: the SDK's Vulkan Xenos GPU on the PS4's RADV, presented through VideoOut.
    add_executable(allstars ../app/main.cpp ../app/ps4_ui.cpp ../audio/ps4_audio.cpp ../input/ps4_input_driver.cpp
        stubs.cpp ${WWE_AUTO_STUBS})
    target_include_directories(allstars PRIVATE ${WWE_DIR} ${WWE_DIR}/src ${WWE_DIR}/generated/default
        ${CMAKE_CURRENT_LIST_DIR}/../app ${CMAKE_CURRENT_LIST_DIR}/..)
    target_link_libraries(allstars PRIVATE -lSceAudioOut -lScePad -lSceUserService)
    target_compile_definitions(allstars PRIVATE REX_HAS_VULKAN=1)
    target_link_libraries(allstars PRIVATE wwe_recomp rexps4)
    target_link_options(allstars PRIVATE --error-limit=0)
    ps4_kit_link_mesa(allstars)
    ps4_kit_app(allstars)
endif()
