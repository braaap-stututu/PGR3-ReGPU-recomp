cmake_minimum_required(VERSION 3.25)
project(pgr3 C CXX)

# Project Gotham Racing 3 (Europe disc, default.xex version 0xB) on ReXenon + ReGPU.
# Layout and build flags follow the FM3 ReGPU port. Phase 2: only the recompiled code (PGR3_PPC_ONLY);
# the runtime (src/, ReGPU, launcher) comes in Phase 3.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "pgr3 needs clang-cl (the recompiled code uses clang builtins)")
endif()
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

# The recompiler is ReXenon (thirdparty/ReXenon), the only one this port uses: out/ppc from tools/recomp.ps1.
# Its timebase comes from PPC_MFTB() in src/ppc_hooks.h.
set(PGR3_XENON_DIR "thirdparty/ReXenon")
set(PGR3_PPC_DIR "${CMAKE_SOURCE_DIR}/out/ppc")
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${PGR3_XENON_DIR}/XenonUtils/ppc_context.h")
    message(FATAL_ERROR "submodules missing: run tools/setup.ps1")
endif()
add_subdirectory(${PGR3_XENON_DIR} EXCLUDE_FROM_ALL)

# Recompiled game code, produced from your own default.xex (tools/recomp.ps1 -Base). Never committed.
file(GLOB PGR3_PPC_SOURCES CONFIGURE_DEPENDS "${PGR3_PPC_DIR}/*.cpp")
if(NOT PGR3_PPC_SOURCES)
    message(FATAL_ERROR "${PGR3_PPC_DIR} is empty: run tools/recomp.ps1 -Base first")
endif()
add_library(pgr3_ppc STATIC ${PGR3_PPC_SOURCES})
target_include_directories(pgr3_ppc PUBLIC "${PGR3_PPC_DIR}" ${PGR3_XENON_DIR}/thirdparty/simde)
target_compile_options(pgr3_ppc PRIVATE -Wno-everything -gline-tables-only "/FI${CMAKE_SOURCE_DIR}/src/ppc_hooks.h")
target_compile_options(pgr3_ppc PUBLIC -msse4.1 -fno-strict-aliasing)

# Weak import stubs (tools/genstubs.py), regenerated from out/ppc so the list always matches.
set(PGR3_GENERATED "${CMAKE_BINARY_DIR}/generated")
add_custom_command(OUTPUT "${PGR3_GENERATED}/stubs.cpp"
    COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_SOURCE_DIR}/tools/genstubs.py" "${PGR3_PPC_DIR}/ppc_recomp_shared.h" "${PGR3_GENERATED}/stubs.cpp"
    DEPENDS "${PGR3_PPC_DIR}/ppc_recomp_shared.h" "${CMAKE_SOURCE_DIR}/tools/genstubs.py"
    COMMENT "Generating import stubs")

# PGR3_PPC_ONLY: build only the recompiled code (a compile check of out/ppc, needs neither src/ nor FidelityFX).
option(PGR3_PPC_ONLY "Build only pgr3_ppc (the recompiled code)" OFF)
if(PGR3_PPC_ONLY)
    return()
endif()

# Runtime sources (ported from the FM3 port's runtime, which builds against the same ReGPU and ReXenon commits).
# Address-bound hooks live in src/hooks and are addressed for PGR3's disc XEX only.
file(GLOB_RECURSE PGR3_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(APPEND PGR3_SOURCES "${PGR3_GENERATED}/stubs.cpp")

# The GPU: ReGPU (thirdparty/ReGPU, MIT, our patch stack), a static library providing the Vd* imports. It builds
# against ReXenon and the game's ppc_config.h, and needs the AMD FidelityFX SDK (MIT) for FSR 1.
set(PGR3_FFX "${CMAKE_SOURCE_DIR}/thirdparty/fidelityfx/Kits/FidelityFX")
if(NOT EXISTS "${PGR3_FFX}/upscalers/fsr3/include/gpu/fsr1/ffx_fsr1.h")
    message(FATAL_ERROR "thirdparty/fidelityfx is missing: put FidelityFX-SDK v2.x there (a junction to the FM3 port's copy works)")
endif()
set(REGPU_XENONRECOMP "${CMAKE_SOURCE_DIR}/${PGR3_XENON_DIR}")
set(REGPU_PPC_DIR "${PGR3_PPC_DIR}")
set(REGPU_FFX "${PGR3_FFX}")
if(EXISTS "${PGR3_FFX}/signedbin/amd_fidelityfx_upscaler_dx12.dll")
    set(REGPU_FSR3 ON)
endif()
set(REGPU_AA_DIR "${CMAKE_SOURCE_DIR}/thirdparty/aa" CACHE PATH "" FORCE)
if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/meshoptimizer/src/meshoptimizer.h")
    set(REGPU_MESHOPT_DIR "${CMAKE_SOURCE_DIR}/thirdparty/meshoptimizer" CACHE PATH "" FORCE)
endif()
add_subdirectory("${CMAKE_SOURCE_DIR}/thirdparty/ReGPU" regpu)

add_executable(pgr3 ${PGR3_SOURCES})
target_include_directories(pgr3 PRIVATE src "${PGR3_PPC_DIR}" ${PGR3_XENON_DIR}/thirdparty/simde)
target_link_libraries(pgr3 PRIVATE regpu pgr3_ppc XenonUtils synchronization winmm onecore dbghelp xaudio2 bcrypt dinput8 dxguid)
target_compile_definitions(pgr3 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS PGR3_REGPU_FM4=0)
target_compile_options(pgr3 PRIVATE -gline-tables-only)
target_link_options(pgr3 PRIVATE /STACK:0x1000000 /DEBUG)
if(REGPU_FSR3)
    add_custom_command(TARGET pgr3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PGR3_FFX}/signedbin/amd_fidelityfx_loader_dx12.dll" "${PGR3_FFX}/signedbin/amd_fidelityfx_upscaler_dx12.dll" $<TARGET_FILE_DIR:pgr3>)
endif()
# ReGPU compiles the translated shaders with the redistributable d3dcompiler_47.dll beside the exe.
if(EXISTS "${REGPU_D3DCOMPILER}")
    add_custom_command(TARGET pgr3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${REGPU_D3DCOMPILER}" $<TARGET_FILE_DIR:pgr3>)
endif()
get_filename_component(PGR3_D3D_REDIST "${REGPU_D3DCOMPILER}" DIRECTORY)
if(EXISTS "${PGR3_D3D_REDIST}/dxcompiler.dll" AND EXISTS "${PGR3_D3D_REDIST}/dxil.dll")
    add_custom_command(TARGET pgr3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PGR3_D3D_REDIST}/dxcompiler.dll" "${PGR3_D3D_REDIST}/dxil.dll" $<TARGET_FILE_DIR:pgr3>)
endif()

# XMA audio through FFmpeg's xma2 decoder (LGPL, linked dynamically): a win64 LGPL shared build in thirdparty/ffmpeg.
set(PGR3_FFMPEG "${CMAKE_SOURCE_DIR}/thirdparty/ffmpeg")
if(NOT EXISTS "${PGR3_FFMPEG}/include/libavcodec/avcodec.h")
    message(FATAL_ERROR "thirdparty/ffmpeg is missing: an FFmpeg win64 LGPL shared build (a junction to the FM3 port's copy works)")
endif()
target_include_directories(pgr3 PRIVATE "${PGR3_FFMPEG}/include")
target_link_libraries(pgr3 PRIVATE "${PGR3_FFMPEG}/lib/avcodec.lib" "${PGR3_FFMPEG}/lib/avutil.lib" winhttp)
file(GLOB PGR3_FFMPEG_DLLS "${PGR3_FFMPEG}/bin/avcodec-*.dll" "${PGR3_FFMPEG}/bin/avutil-*.dll" "${PGR3_FFMPEG}/bin/swresample-*.dll")
add_custom_command(TARGET pgr3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PGR3_FFMPEG_DLLS} $<TARGET_FILE_DIR:pgr3>)
