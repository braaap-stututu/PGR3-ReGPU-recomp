// GPU adapter and memory log (session 18, RESEARCH.md "Less RAM, VRAM and memory traffic" step 1 and
// "Choosing the GPU in the launcher" step 4). Measurement only; changes nothing the game or ReGPU does.
//  - At start: every DXGI adapter (name, PCI ids, dedicated/shared memory) and which one ReGPU renders on
//    (Windows' default is adapter 0; ReGPU's own line "presenter: rendering on ..." names the one it uses).
//  - Every 30 s: this process's video memory use against the OS budget (QueryVideoMemoryInfo: local = the
//    GPU's own memory, or an iGPU's carve-out; non-local = shared system memory), and its RAM (working
//    set, private bytes), each with the peak so far. PGR3_NO_GPU_MEMORY_LOG=1: off.
// Vulkan ReGPU (CMake PGR3_REGPU_DIR = thirdparty/ReGPU-vk, session 6): ReGPU's GetGpuMemory (VK_EXT_memory_budget:
// local = the GPU's own memory, or an iGPU's carve-out; shared = system memory the GPU reads) instead of DXGI; the
// first line names the GPU (ReGPU's own log lists every Vulkan device). As the FM3 port's regpu-vulkan branch.
#if PGR3_REGPU_VULKAN
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <thread>

#include <windows.h>
#include <psapi.h>

#include "log.h"
#include "regpu/regpu.h"

namespace
{
uint64_t Mb(uint64_t bytes) { return bytes >> 20; }
}

void StartGpuMemoryLog()
{
    if (getenv("PGR3_NO_GPU_MEMORY_LOG"))
        return;
    std::thread([] {
        SetThreadDescription(GetCurrentThread(), L"pgr3 gpu memory log");
        uint64_t peakLocal = 0, peakShared = 0, peakPrivate = 0;
        bool named = false;
        for (;;)
        {
            Sleep(30000);
            regpu::GpuMemory g;
            if (!regpu::GetGpuMemory(g))
                continue;
            if (!named)
            {
                Log("gpu: rendering on %s (%04X:%04X)%s", g.name, g.vendorId, g.deviceId, g.integrated ? ", integrated" : "");
                named = true;
            }
            PROCESS_MEMORY_COUNTERS_EX pmc{};
            pmc.cb = sizeof(pmc);
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
            peakLocal = std::max<uint64_t>(peakLocal, g.localUsage);
            peakShared = std::max<uint64_t>(peakShared, g.sharedUsage);
            peakPrivate = std::max<uint64_t>(peakPrivate, pmc.PrivateUsage);
            Log("gpu memory: local %llu / budget %llu MB (peak %llu), shared %llu / %llu MB (peak %llu), RAM working set %llu MB, "
                "private %llu MB (peak %llu)",
                (unsigned long long)Mb(g.localUsage), (unsigned long long)Mb(g.localBudget), (unsigned long long)Mb(peakLocal),
                (unsigned long long)Mb(g.sharedUsage), (unsigned long long)Mb(g.sharedBudget), (unsigned long long)Mb(peakShared),
                (unsigned long long)Mb(pmc.WorkingSetSize), (unsigned long long)Mb(pmc.PrivateUsage), (unsigned long long)Mb(peakPrivate));
        }
    }).detach();
}
#else
#include <cstdint>
#include <thread>

#include <windows.h>
#include <dxgi1_6.h>
#include <psapi.h>
#include <wrl/client.h>

#include "log.h"
#include <d3d12.h>

ID3D12Device* RenderDevice(); // ReGPU (render/presenter.h): its device, made on first use

#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace
{
uint64_t Mb(uint64_t bytes) { return bytes >> 20; }

void LogAdapters(IDXGIFactory1* factory)
{
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++)
    {
        DXGI_ADAPTER_DESC1 d{};
        adapter->GetDesc1(&d);
        char name[128] = {};
        WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, sizeof(name) - 1, nullptr, nullptr);
        Log("gpu: adapter %u%s: %s (%04X:%04X), dedicated %llu MB, shared %llu MB%s", i, i == 0 ? " (Windows' default)" : "", name,
            d.VendorId, d.DeviceId, (unsigned long long)Mb(d.DedicatedVideoMemory), (unsigned long long)Mb(d.SharedSystemMemory),
            (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? ", software" : "");
    }
}
}

void StartGpuMemoryLog()
{
    if (getenv("PGR3_NO_GPU_MEMORY_LOG"))
        return;
    std::thread([] {
        SetThreadDescription(GetCurrentThread(), L"pgr3 gpu memory log");
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
            return;
        LogAdapters(factory.Get());
        ComPtr<IDXGIAdapter3> adapter;
        uint64_t peakLocal = 0, peakShared = 0, peakPrivate = 0;
        for (;;)
        {
            Sleep(30000);
            // The adapter ReGPU's device is on (REGPU_ADAPTER, settings.ini gpu=: not always adapter 0); made by now.
            if (!adapter)
            {
                ComPtr<IDXGIFactory4> f4;
                if (ID3D12Device* device = RenderDevice(); !device || FAILED(factory.As(&f4)) ||
                    FAILED(f4->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(&adapter))))
                    return;
            }
            DXGI_QUERY_VIDEO_MEMORY_INFO local{}, shared{};
            adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
            adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &shared);
            PROCESS_MEMORY_COUNTERS_EX pmc{};
            pmc.cb = sizeof(pmc);
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
            peakLocal = std::max<uint64_t>(peakLocal, local.CurrentUsage);
            peakShared = std::max<uint64_t>(peakShared, shared.CurrentUsage);
            peakPrivate = std::max<uint64_t>(peakPrivate, pmc.PrivateUsage);
            Log("gpu memory: local %llu / budget %llu MB (peak %llu), shared %llu / %llu MB (peak %llu), RAM working set %llu MB, "
                "private %llu MB (peak %llu)",
                (unsigned long long)Mb(local.CurrentUsage), (unsigned long long)Mb(local.Budget), (unsigned long long)Mb(peakLocal),
                (unsigned long long)Mb(shared.CurrentUsage), (unsigned long long)Mb(shared.Budget), (unsigned long long)Mb(peakShared),
                (unsigned long long)Mb(pmc.WorkingSetSize), (unsigned long long)Mb(pmc.PrivateUsage), (unsigned long long)Mb(peakPrivate));
        }
    }).detach();
}
#endif // PGR3_REGPU_VULKAN
