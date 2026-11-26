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
# MSVC's <atomic> (14.4x) marks every function target("cx16") under clang, and clang never inlines a function into a
# caller with fewer target features: without -mcx16 every std::atomic load/store/increment was an out-of-line call
# (~300 call sites in ReGPU's hot paths). Every x64 CPU Windows 8.1+ runs on has CMPXCHG16B. PGR3 session 4.
option(PGR3_NO_CX16 "build without -mcx16 (A/B)" OFF)
if(NOT PGR3_NO_CX16)
    add_compile_options(-mcx16)
endif()

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
# PGR3_REGPU_DIR: which ReGPU to build. thirdparty/ReGPU: the Direct3D 12 ReGPU with our patch stack (tools/setup.ps1). thirdparty/ReGPU-vk (session 6): the Vulkan ReGPU (github.com/braaap-stututu/ReGPU, local branch
# pgr3vk = fm4deck + main + the PGR3 fixes), as the FM3 and FM4 ports use; it translates shaders to SPIR-V and needs
# dxcompiler.dll + dxil.dll with SPIR-V output beside pgr3.exe (PGR3_DXC_DIR) and dxc for its own compute shaders
# (REGPU_DXC_EXE). Told apart by src/gpu/vk.
# Default (session 6, the user's request): the Vulkan ReGPU when thirdparty/ReGPU-vk is there, else the D3D12 one.
if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/ReGPU-vk/src/gpu/vk")
    set(PGR3_REGPU_DEFAULT "${CMAKE_SOURCE_DIR}/thirdparty/ReGPU-vk")
else()
    set(PGR3_REGPU_DEFAULT "${CMAKE_SOURCE_DIR}/thirdparty/ReGPU")
    message(WARNING "thirdparty/ReGPU-vk is missing (tools/setup.ps1 makes it): this build uses the Direct3D 12 ReGPU, but the launcher runs build/rel as the Vulkan one")
endif()
set(PGR3_REGPU_DIR "${PGR3_REGPU_DEFAULT}" CACHE PATH "ReGPU source folder")
if(EXISTS "${PGR3_REGPU_DIR}/src/gpu/vk")
    set(PGR3_REGPU_VULKAN ON)
    set(PGR3_DXC_DIR "${CMAKE_SOURCE_DIR}/thirdparty/dxc/bin/x64" CACHE PATH "Folder with dxcompiler.dll and dxil.dll (SPIR-V output)")
    # dxc.exe from the same release builds ReGPU-V's own compute shaders to SPIR-V on Windows (as the FM3 port; on the
    # Deck pass -DREGPU_DXC_EXE=/usr/bin/dxc). Else ReGPU looks in the Vulkan SDK and on PATH.
    if(EXISTS "${PGR3_DXC_DIR}/dxc.exe" AND CMAKE_HOST_WIN32)
        set(REGPU_DXC_EXE "${PGR3_DXC_DIR}/dxc.exe" CACHE FILEPATH "DXC with SPIR-V output (ReGPU's embedded compute shaders)")
    endif()
    if(NOT EXISTS "${PGR3_DXC_DIR}/dxcompiler.dll")
        message(WARNING "${PGR3_DXC_DIR}/dxcompiler.dll is missing: the Vulkan ReGPU needs a DirectXShaderCompiler release beside pgr3.exe")
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/xess/inc/xess/xess_vk.h")
        set(REGPU_XESS_DIR "${CMAKE_SOURCE_DIR}/thirdparty/xess" CACHE PATH "" FORCE)
    endif()
    set(PGR3_FFX_API vk)
else()
    set(PGR3_REGPU_VULKAN OFF)
    set(PGR3_FFX_API dx12)
endif()
set(REGPU_XENONRECOMP "${CMAKE_SOURCE_DIR}/${PGR3_XENON_DIR}")
set(REGPU_PPC_DIR "${PGR3_PPC_DIR}")
set(REGPU_FFX "${PGR3_FFX}")
if(EXISTS "${PGR3_FFX}/signedbin/amd_fidelityfx_upscaler_${PGR3_FFX_API}.dll")
    set(REGPU_FSR3 ON)
endif()
set(REGPU_AA_DIR "${CMAKE_SOURCE_DIR}/thirdparty/aa" CACHE PATH "" FORCE)
if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/meshoptimizer/src/meshoptimizer.h")
    set(REGPU_MESHOPT_DIR "${CMAKE_SOURCE_DIR}/thirdparty/meshoptimizer" CACHE PATH "" FORCE)
endif()
# VR (session 7, as the FM3 port's session 37; ReGPU src/render/xr.cpp, settings.ini vr=screen|cockpit|stereo): the
# OpenXR SDK's headers (include/openxr) and loader (bin/openxr_loader.dll) in thirdparty/openxr (Khronos, Apache-2.0;
# not in git). Without it the game builds without VR.
set(PGR3_OPENXR "${CMAKE_SOURCE_DIR}/thirdparty/openxr")
if(EXISTS "${PGR3_OPENXR}/include/openxr/openxr.h")
    set(REGPU_OPENXR_DIR "${PGR3_OPENXR}" CACHE PATH "" FORCE)
endif()
add_subdirectory("${PGR3_REGPU_DIR}" regpu)
# Extra compile flags for ReGPU (A/B of code generation, e.g. -DPGR3_REGPU_FLAGS="-mllvm;-inline-threshold=1000").
set(PGR3_REGPU_FLAGS "" CACHE STRING "extra compile options for the regpu library")
if(PGR3_REGPU_FLAGS)
    target_compile_options(regpu PRIVATE ${PGR3_REGPU_FLAGS})
endif()

add_executable(pgr3 ${PGR3_SOURCES})
target_include_directories(pgr3 PRIVATE src "${PGR3_PPC_DIR}" ${PGR3_XENON_DIR}/thirdparty/simde)
target_link_libraries(pgr3 PRIVATE regpu pgr3_ppc XenonUtils synchronization winmm onecore dbghelp xaudio2 bcrypt dinput8 dxguid ws2_32)
target_compile_definitions(pgr3 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS PGR3_REGPU_FM4=0
    PGR3_REGPU_VULKAN=$<BOOL:${PGR3_REGPU_VULKAN}>)
target_compile_options(pgr3 PRIVATE -gline-tables-only)
target_link_options(pgr3 PRIVATE /STACK:0x1000000 /DEBUG)
if(REGPU_FSR3)
    add_custom_command(TARGET pgr3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PGR3_FFX}/signedbin/amd_fidelityfx_loader_${PGR3_FFX_API}.dll" "${PGR3_FFX}/signedbin/amd_fidelityfx_upscaler_${PGR3_FFX_API}.dll" $<TARGET_FILE_DIR:pgr3>)
endif()
if(PGR3_REGPU_VULKAN)
    # The Vulkan ReGPU compiles translated shaders (those its direct SPIR-V translator leaves to HLSL) with DXC.
    foreach(PGR3_DXC_DLL dxcompiler.dll dxil.dll)
        if(EXISTS "${PGR3_DXC_DIR}/${PGR3_DXC_DLL}")
            add_custom_command(TARGET pgr3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PGR3_DXC_DIR}/${PGR3_DXC_DLL}" $<TARGET_FILE_DIR:pgr3>)
        endif()
    endforeach()
    if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/xess/bin/libxess.dll")
        add_custom_command(TARGET pgr3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/thirdparty/xess/bin/libxess.dll" $<TARGET_FILE_DIR:pgr3>)
    endif()
# ReGPU compiles the translated shaders with the redistributable d3dcompiler_47.dll beside the exe.
elseif(EXISTS "${REGPU_D3DCOMPILER}")
    add_custom_command(TARGET pgr3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${REGPU_D3DCOMPILER}" $<TARGET_FILE_DIR:pgr3>)
endif()
get_filename_component(PGR3_D3D_REDIST "${REGPU_D3DCOMPILER}" DIRECTORY)
if(NOT PGR3_REGPU_VULKAN AND EXISTS "${PGR3_D3D_REDIST}/dxcompiler.dll" AND EXISTS "${PGR3_D3D_REDIST}/dxil.dll")
    add_custom_command(TARGET pgr3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PGR3_D3D_REDIST}/dxcompiler.dll" "${PGR3_D3D_REDIST}/dxil.dll" $<TARGET_FILE_DIR:pgr3>)
endif()

# XMA audio through FFmpeg's xma2 decoder (LGPL, linked dynamically): a win64 LGPL shared build in thirdparty/ffmpeg.
set(PGR3_FFMPEG "${CMAKE_SOURCE_DIR}/thirdparty/ffmpeg")
if(NOT EXISTS "${PGR3_FFMPEG}/include/libavcodec/avcodec.h")
    message(FATAL_ERROR "thirdparty/ffmpeg is missing: an FFmpeg win64 LGPL shared build (a junction to the FM3 port's copy works)")
endif()
target_include_directories(pgr3 PRIVATE "${PGR3_FFMPEG}/include")
# avformat + swresample (session 6): the media player's music (src/audio/xmp.cpp: ASF/WMA songs, resampled to 48 kHz).
target_link_libraries(pgr3 PRIVATE "${PGR3_FFMPEG}/lib/avcodec.lib" "${PGR3_FFMPEG}/lib/avutil.lib" "${PGR3_FFMPEG}/lib/avformat.lib"
    "${PGR3_FFMPEG}/lib/swresample.lib" winhttp)
file(GLOB PGR3_FFMPEG_DLLS "${PGR3_FFMPEG}/bin/avcodec-*.dll" "${PGR3_FFMPEG}/bin/avutil-*.dll" "${PGR3_FFMPEG}/bin/swresample-*.dll"
    "${PGR3_FFMPEG}/bin/avformat-*.dll")
add_custom_command(TARGET pgr3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PGR3_FFMPEG_DLLS} $<TARGET_FILE_DIR:pgr3>)
if(EXISTS "${PGR3_OPENXR}/bin/openxr_loader.dll")
    add_custom_command(TARGET pgr3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PGR3_OPENXR}/bin/openxr_loader.dll" $<TARGET_FILE_DIR:pgr3>)
endif()
