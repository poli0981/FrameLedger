// The Vulkan layer's capture side (P1 item 3): vkQueuePresentKHR intercepted,
// the ring written, the in-layer supervision stop (§S2 part three) -- driven
// end to end by a FAKE loader chain, exactly as 20_OPEN_QUESTIONS §S2 said it
// would land: "in the PR that adds the present hook, where a fake loader chain
// can drive it end to end".
//
// Three ctests, three processes, because a process carries ONE ring and each
// flavour of the DLL creates one:
//   [vklayer]           the shipped DLL: records, forwarding, the unhook stop
//   [vklayer-deadline]  the short-deadline flavour: guardTicks stops -> the layer stops
//   [vklayer-real]      the REAL loader, the REAL harness (--vulkan), via
//                       VK_ADD_IMPLICIT_LAYER_PATH -- SKIPPED where there is no
//                       Vulkan loader or no presentable device (CI, §S29(d))
//
// The layer reads its enable-list and rules from the ONE product location under
// %LOCALAPPDATA% and there is deliberately no override (§S3), so this test writes
// its own image name into that enable-list and, when no rules file is installed,
// the repository seed -- and removes exactly what it added. It never touches an
// existing rules file.

#include <windows.h>

#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstring>
#include <fl_ac_rules.h>
#include <fl_shm.h>
#include <string>

namespace {

// ---------------------------------------------------------------------------
// The product's files: enable-list + rules, added and removed by this test.
// ---------------------------------------------------------------------------
struct ProductFiles {
    std::wstring enableList;
    std::string  enableListBackup;    // the previous contents, when there were any
    bool         hadEnableList = false;
    std::wstring rules;
    bool         installedRules = false;

    static std::wstring LocalAppData() {
        wchar_t base[fl::guard::kMaxRulesPathLen]{};
        REQUIRE(fl::guard::LocalAppDataDir(base, fl::guard::kMaxRulesPathLen));
        return base;
    }

    static void EnsureDir(const std::wstring& dir) {
        std::wstring cur;
        for (size_t i = 0; i < dir.size(); ++i) {
            cur += dir[i];
            if ((dir[i] == L'\\' || i + 1 == dir.size()) && cur.size() > 3) {
                CreateDirectoryW(cur.c_str(), nullptr);
            }
        }
    }

    static bool ReadAll(const std::wstring& path, std::string& out) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            return false;
        }
        char  buf[4096];
        DWORD n = 0;
        out.clear();
        while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0) {
            out.append(buf, n);
        }
        CloseHandle(h);
        return true;
    }

    static bool WriteAll(const std::wstring& path, const std::string& text) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            return false;
        }
        DWORD      n = 0;
        const BOOL ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &n, nullptr);
        CloseHandle(h);
        return ok && n == text.size();
    }

    // Adds `imageName` to the enable-list (creating it if needed) and installs the
    // repository seed rules when no rules file exists.
    void Install(const char* imageName) {
        const std::wstring base = LocalAppData();
        const std::wstring dir = base + L"\\FrameLedger\\vklayer";
        EnsureDir(dir);
        enableList = dir + L"\\enabled.txt";
        std::string existing;
        hadEnableList = ReadAll(enableList, existing);
        if (hadEnableList) {
            enableListBackup = existing;
        }
        std::string text = existing;
        if (!text.empty() && text.back() != '\n') {
            text += '\n';
        }
        text += "# added by fl_vklayer_test, removed when it exits\n";
        text += imageName;
        text += '\n';
        REQUIRE(WriteAll(enableList, text));

        wchar_t rulesPath[fl::guard::kMaxRulesPathLen]{};
        REQUIRE(fl::guard::RulesFilePath(rulesPath, fl::guard::kMaxRulesPathLen));
        rules = rulesPath;
        if (GetFileAttributesW(rulesPath) == INVALID_FILE_ATTRIBUTES) {
            const std::wstring rulesDir = rules.substr(0, rules.find_last_of(L'\\'));
            EnsureDir(rulesDir);
            REQUIRE(CopyFileW(FL_REPO_RULES, rulesPath, TRUE));
            installedRules = true;
        }
    }

    ~ProductFiles() {
        if (!enableList.empty()) {
            if (hadEnableList) {
                WriteAll(enableList, enableListBackup);
            } else {
                DeleteFileW(enableList.c_str());
            }
        }
        if (installedRules) {
            DeleteFileW(rules.c_str());
        }
    }
};

std::string OwnImageName() {
    char full[MAX_PATH]{};
    REQUIRE(GetModuleFileNameA(nullptr, full, MAX_PATH) != 0);
    const char* leaf = std::strrchr(full, '\\');
    return leaf != nullptr ? leaf + 1 : full;
}

// ---------------------------------------------------------------------------
// The fake loader chain. Dispatchable handles are objects whose FIRST word is the
// dispatch key, which is what the loader guarantees and what the layer keys on;
// the device and its queue share one key, as they do under a real loader.
// ---------------------------------------------------------------------------
void* g_deviceKey = &g_deviceKey;
void* g_instanceKey = &g_instanceKey;
struct Dispatchable {
    void* key;
};
Dispatchable g_instanceObj{g_instanceKey};
Dispatchable g_deviceObj{g_deviceKey};
Dispatchable g_queueObj{g_deviceKey};

std::atomic<int>     g_forwardedPresents{0};
std::atomic<int>     g_destroyed{0};
VkSwapchainKHR       g_lastPresented = VK_NULL_HANDLE;
const VkSwapchainKHR kFakeSwapchain = reinterpret_cast<VkSwapchainKHR>(static_cast<uintptr_t>(0x1234));

VKAPI_ATTR VkResult VKAPI_CALL FakeCreateInstance(const VkInstanceCreateInfo*, const VkAllocationCallbacks*,
                                                  VkInstance* pInstance) {
    *pInstance = reinterpret_cast<VkInstance>(&g_instanceObj);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL FakeCreateDevice(VkPhysicalDevice, const VkDeviceCreateInfo*,
                                                const VkAllocationCallbacks*, VkDevice* pDevice) {
    *pDevice = reinterpret_cast<VkDevice>(&g_deviceObj);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL FakeQueuePresent(VkQueue, const VkPresentInfoKHR* info) {
    ++g_forwardedPresents;
    g_lastPresented = (info != nullptr && info->swapchainCount > 0) ? info->pSwapchains[0] : VK_NULL_HANDLE;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL FakeCreateSwapchain(VkDevice, const VkSwapchainCreateInfoKHR*,
                                                   const VkAllocationCallbacks*, VkSwapchainKHR* out) {
    *out = kFakeSwapchain;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL FakeDestroySwapchain(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks*) {
    ++g_destroyed;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL FakeGetDeviceProcAddr(VkDevice, const char* name) {
    if (std::strcmp(name, "vkQueuePresentKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&FakeQueuePresent);
    }
    if (std::strcmp(name, "vkCreateSwapchainKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&FakeCreateSwapchain);
    }
    if (std::strcmp(name, "vkDestroySwapchainKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&FakeDestroySwapchain);
    }
    return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL FakeGetInstanceProcAddr(VkInstance, const char* name) {
    if (std::strcmp(name, "vkCreateInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&FakeCreateInstance);
    }
    if (std::strcmp(name, "vkCreateDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&FakeCreateDevice);
    }
    return nullptr;
}

// The layer, driven as the loader would drive it: negotiate, create an instance
// through the chain, create a device through the chain, fetch the device entries.
struct DrivenLayer {
    HMODULE                   dll = nullptr;
    PFN_vkGetInstanceProcAddr gipa = nullptr;
    PFN_vkGetDeviceProcAddr   gdpa = nullptr;
    VkInstance                instance = nullptr;
    VkDevice                  device = nullptr;
    PFN_vkQueuePresentKHR     present = nullptr;
    PFN_vkCreateSwapchainKHR  createSwapchain = nullptr;
    PFN_vkDestroySwapchainKHR destroySwapchain = nullptr;

    void Drive(const wchar_t* path) {
        dll = LoadLibraryW(path);
        REQUIRE(dll != nullptr);
        auto negotiate = reinterpret_cast<PFN_vkNegotiateLoaderLayerInterfaceVersion>(
            GetProcAddress(dll, "vkNegotiateLoaderLayerInterfaceVersion"));
        REQUIRE(negotiate != nullptr);
        VkNegotiateLayerInterface n{};
        n.sType = LAYER_NEGOTIATE_INTERFACE_STRUCT;
        n.loaderLayerInterfaceVersion = 2;
        REQUIRE(negotiate(&n) == VK_SUCCESS);
        gipa = n.pfnGetInstanceProcAddr;
        gdpa = n.pfnGetDeviceProcAddr;
        REQUIRE(gipa != nullptr);
        REQUIRE(gdpa != nullptr);

        VkLayerInstanceLink       ilink{nullptr, &FakeGetInstanceProcAddr, nullptr};
        VkLayerInstanceCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO;
        ici.function = VK_LAYER_LINK_INFO;
        ici.u.pLayerInfo = &ilink;
        VkInstanceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pNext = &ici;
        auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance"));
        REQUIRE(createInstance != nullptr);
        REQUIRE(createInstance(&ci, nullptr, &instance) == VK_SUCCESS);
        REQUIRE(instance == reinterpret_cast<VkInstance>(&g_instanceObj));

        VkLayerDeviceLink       dlink{nullptr, &FakeGetInstanceProcAddr, &FakeGetDeviceProcAddr};
        VkLayerDeviceCreateInfo dci{};
        dci.sType = VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO;
        dci.function = VK_LAYER_LINK_INFO;
        dci.u.pLayerInfo = &dlink;
        VkDeviceCreateInfo dc{};
        dc.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dc.pNext = &dci;
        auto createDevice = reinterpret_cast<PFN_vkCreateDevice>(gipa(instance, "vkCreateDevice"));
        REQUIRE(createDevice != nullptr);
        REQUIRE(createDevice(nullptr, &dc, nullptr, &device) == VK_SUCCESS);
        REQUIRE(device == reinterpret_cast<VkDevice>(&g_deviceObj));

        present = reinterpret_cast<PFN_vkQueuePresentKHR>(gdpa(device, "vkQueuePresentKHR"));
        createSwapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR>(gdpa(device, "vkCreateSwapchainKHR"));
        destroySwapchain = reinterpret_cast<PFN_vkDestroySwapchainKHR>(gdpa(device, "vkDestroySwapchainKHR"));
    }

    VkResult PresentOnce(VkSwapchainKHR sc) const {
        uint32_t         index = 0;
        VkPresentInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        info.swapchainCount = 1;
        info.pSwapchains = &sc;
        info.pImageIndices = &index;
        return present(reinterpret_cast<VkQueue>(&g_queueObj), &info);
    }
};

struct OwnRing {
    HANDLE                   mapping = nullptr;
    void*                    base = nullptr;
    fl::FlShmHandshake*      hs = nullptr;
    fl::FlWriterState*       st = nullptr;
    fl::FlControlBlock*      ctl = nullptr;
    const fl::FlFrameRecord* ring = nullptr;

    bool Open() {
        wchar_t name[128]{};
        _snwprintf_s(name, _TRUNCATE, L"Local\\FrameLedger.Ring.%lu", GetCurrentProcessId());
        mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
        if (mapping == nullptr) {
            return false;
        }
        base = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
        if (base == nullptr) {
            return false;
        }
        auto* bytes = static_cast<unsigned char*>(base);
        hs = reinterpret_cast<fl::FlShmHandshake*>(bytes + FL_SHM_HANDSHAKE_OFFSET);
        st = reinterpret_cast<fl::FlWriterState*>(bytes + FL_SHM_WRITER_OFFSET);
        ctl = reinterpret_cast<fl::FlControlBlock*>(bytes + FL_SHM_CONTROL_OFFSET);
        ring = reinterpret_cast<const fl::FlFrameRecord*>(bytes + FL_SHM_RING_OFFSET);
        return true;
    }
    ~OwnRing() {
        if (base != nullptr) {
            UnmapViewOfFile(base);
        }
        if (mapping != nullptr) {
            CloseHandle(mapping);
        }
    }
};

}    // namespace

// ===========================================================================
TEST_CASE("the layer, driven by a fake loader chain, owns the ring and records every present it forwards",
          "[vklayer]") {
    ProductFiles files;
    files.Install(OwnImageName().c_str());    // BEFORE the DLL loads: both gates are cached on first use

    DrivenLayer layer;
    layer.Drive(FL_VKLAYER_DLL);
    REQUIRE(layer.present != nullptr);
    CHECK(layer.present != &FakeQueuePresent);    // ours, not the next layer's
    REQUIRE(layer.createSwapchain != nullptr);
    REQUIRE(layer.destroySwapchain != nullptr);

    // The ring exists under OUR pid, with the handshake a reader validates first.
    OwnRing r;
    REQUIRE(r.Open());
    CHECK(r.hs->layoutVersion == FL_SHM_LAYOUT_VERSION);
    CHECK(r.hs->pid == GetCurrentProcessId());
    CHECK(r.hs->recordSize == sizeof(fl::FlFrameRecord));
    CHECK(r.st->status == fl::FL_STATUS_READY);
    CHECK((r.st->apiMask & (1u << fl::FL_API_VULKAN)) != 0u);
    CHECK((r.st->hooksInstalledMask & fl::FL_HOOK_PRESENT) != 0u);
    CHECK(r.st->dxgiPresentsBeforeHook == 0xFFFFFFFFu);    // no DXGI counter on this path: "not read"

    // A swapchain created through us is remembered with its extent.
    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.imageExtent = VkExtent2D{1280u, 720u};
    VkSwapchainKHR sc = VK_NULL_HANDLE;
    REQUIRE(layer.createSwapchain(layer.device, &sci, nullptr, &sc) == VK_SUCCESS);
    REQUIRE(sc == kFakeSwapchain);

    // Presents: one record each, forwarded each, the record naming the chain and its size.
    for (int i = 0; i < 5; ++i) {
        ++r.ctl->guardTicks;
        REQUIRE(layer.PresentOnce(sc) == VK_SUCCESS);
    }
    CHECK(g_forwardedPresents.load() == 5);
    CHECK(g_lastPresented == kFakeSwapchain);
    CHECK(r.st->writeIndex == 5u);
    const fl::FlFrameRecord& rec = r.ring[0];
    CHECK(rec.api == static_cast<uint8_t>(fl::FL_API_VULKAN));
    CHECK(rec.outputW == 1280u);
    CHECK(rec.outputH == 720u);
    CHECK(rec.swapchainId != 0u);
    CHECK(rec.measuredMask == fl::FL_MEASURED_OUTPUT_RES);    // and NOT PRESENT_ARGS: vkQueuePresentKHR has none
    CHECK(r.ring[4].frameIndex == rec.frameIndex + 4u);
    CHECK(r.ring[4].qpc > rec.qpc);

    // A present on a chain we never saw is still recorded, as "unidentified", and forwarded.
    const auto unknown = reinterpret_cast<VkSwapchainKHR>(static_cast<uintptr_t>(0x9999));
    REQUIRE(layer.PresentOnce(unknown) == VK_SUCCESS);
    CHECK(r.st->writeIndex == 6u);
    CHECK(r.ring[5].swapchainId == 0u);
    CHECK(r.ring[5].measuredMask == 0u);

    // Destroyed through us: forgotten, and the next layer told.
    layer.destroySwapchain(layer.device, sc, nullptr);
    CHECK(g_destroyed.load() == 1);
    REQUIRE(layer.PresentOnce(sc) == VK_SUCCESS);
    CHECK(r.ring[6].swapchainId == 0u);

    // Pause is not stop: no record, still forwarded, status untouched.
    r.ctl->pauseRequested = 1;
    REQUIRE(layer.PresentOnce(sc) == VK_SUCCESS);
    CHECK(r.st->writeIndex == 7u);
    CHECK(g_forwardedPresents.load() == 8);
    CHECK(r.st->status == fl::FL_STATUS_READY);
    r.ctl->pauseRequested = 0;

    // THE SAFETY STOP: unhookRequested -> UNHOOKED within one present, writeIndex frozen,
    // and every later present still forwarded -- "stops" is passthrough, never a broken app.
    r.ctl->unhookRequested = 1;
    REQUIRE(layer.PresentOnce(sc) == VK_SUCCESS);
    CHECK(r.st->status == fl::FL_STATUS_UNHOOKED);
    CHECK(r.st->writeIndex == 7u);
    for (int i = 0; i < 3; ++i) {
        REQUIRE(layer.PresentOnce(sc) == VK_SUCCESS);
    }
    CHECK(r.st->writeIndex == 7u);
    CHECK(g_forwardedPresents.load() == 12);
    CHECK(r.st->faultCount == 0u);
}

// ===========================================================================
TEST_CASE("§S2 part three: when guardTicks stops advancing the layer stops itself, and while it advances it does not",
          "[vklayer-deadline]") {
    // The short-deadline flavour (1.5 s instead of 65 s), same sources. The clock
    // starts when the ring is published; ticks that keep coming keep it alive
    // past the deadline, and ticks that stop end it -- on the present path, since
    // the layer owns no thread (§S2).
    ProductFiles files;
    files.Install(OwnImageName().c_str());

    DrivenLayer layer;
    layer.Drive(FL_VKLAYER_SHORTDEADLINE_DLL);
    REQUIRE(layer.present != nullptr);
    OwnRing r;
    REQUIRE(r.Open());
    REQUIRE(r.st->status == fl::FL_STATUS_READY);

    // Supervised: 2.4 s of presents with a tick every 600 ms -- past the 1.5 s deadline, still READY.
    for (int i = 0; i < 4; ++i) {
        ++r.ctl->guardTicks;
        REQUIRE(layer.PresentOnce(kFakeSwapchain) == VK_SUCCESS);
        Sleep(600);
    }
    ++r.ctl->guardTicks;
    REQUIRE(layer.PresentOnce(kFakeSwapchain) == VK_SUCCESS);
    CHECK(r.st->status == fl::FL_STATUS_READY);
    const uint64_t recorded = r.st->writeIndex;
    CHECK(recorded == 5u);

    // Unsupervised: no tick for 1.8 s -> the next present stops the layer.
    Sleep(1800);
    REQUIRE(layer.PresentOnce(kFakeSwapchain) == VK_SUCCESS);
    CHECK(r.st->status == fl::FL_STATUS_UNHOOKED);
    CHECK(r.st->writeIndex == recorded);
    REQUIRE(layer.PresentOnce(kFakeSwapchain) == VK_SUCCESS);    // still forwarded
    CHECK(g_forwardedPresents.load() == 7);
}

// ===========================================================================
namespace {

struct Child {
    PROCESS_INFORMATION pi{};
    ~Child() {
        if (pi.hProcess != nullptr) {
            TerminateProcess(pi.hProcess, 0);
            WaitForSingleObject(pi.hProcess, 3000);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
};

struct ChildRing {
    HANDLE                   mapping = nullptr;
    void*                    base = nullptr;
    fl::FlWriterState*       st = nullptr;
    fl::FlControlBlock*      ctl = nullptr;
    const fl::FlFrameRecord* ring = nullptr;
    ~ChildRing() {
        if (base != nullptr) {
            UnmapViewOfFile(base);
        }
        if (mapping != nullptr) {
            CloseHandle(mapping);
        }
    }
    bool OpenFor(DWORD pid, HANDLE process, int waitMs) {
        wchar_t name[128]{};
        _snwprintf_s(name, _TRUNCATE, L"Local\\FrameLedger.Ring.%lu", pid);
        const ULONGLONG until = GetTickCount64() + static_cast<ULONGLONG>(waitMs);
        while (mapping == nullptr && GetTickCount64() < until) {
            mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
            if (mapping == nullptr) {
                if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
                    return false;    // the child is gone
                }
                Sleep(50);
            }
        }
        if (mapping == nullptr) {
            return false;
        }
        base = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
        if (base == nullptr) {
            return false;
        }
        auto* bytes = static_cast<unsigned char*>(base);
        st = reinterpret_cast<fl::FlWriterState*>(bytes + FL_SHM_WRITER_OFFSET);
        ctl = reinterpret_cast<fl::FlControlBlock*>(bytes + FL_SHM_CONTROL_OFFSET);
        ring = reinterpret_cast<const fl::FlFrameRecord*>(bytes + FL_SHM_RING_OFFSET);
        return true;
    }
};

}    // namespace

TEST_CASE("the REAL loader maps the layer from VK_ADD_IMPLICIT_LAYER_PATH into the harness's Vulkan mode, and every "
          "present is recorded",
          "[vklayer-real]") {
    // No registry: the loader honours VK_ADD_IMPLICIT_LAYER_PATH (measured 2026-09-06
    // against 1.4.357, spike-notes §2), so the layer is discoverable ONLY by the
    // process this test starts. Skipped honestly where there is no loader or no
    // presentable device -- CI has neither (§S29(d)).
    if (LoadLibraryW(L"vulkan-1.dll") == nullptr) {
        SKIP("no Vulkan loader on this machine");
    }

    ProductFiles files;
    files.Install("hook-harness.exe");

    // Environment for the child: inherited from ours, so set, spawn, unset.
    REQUIRE(SetEnvironmentVariableW(L"VK_ADD_IMPLICIT_LAYER_PATH", FL_VKLAYER_MANIFEST_DIR));
    REQUIRE(SetEnvironmentVariableW(L"FRAMELEDGER_ENABLE_VK_LAYER", L"1"));
    Child        child;
    std::wstring cmd = std::wstring(L"\"") + FL_HARNESS_EXE + L"\" --vulkan --hold-presenting 4";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    const BOOL started = CreateProcessW(FL_HARNESS_EXE, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                                        nullptr, &si, &child.pi);
    SetEnvironmentVariableW(L"VK_ADD_IMPLICIT_LAYER_PATH", nullptr);
    SetEnvironmentVariableW(L"FRAMELEDGER_ENABLE_VK_LAYER", nullptr);
    REQUIRE(started);

    ChildRing  r;
    const bool ringUp = r.OpenFor(child.pi.dwProcessId, child.pi.hProcess, 15000);
    if (!ringUp) {
        DWORD code = 0;
        WaitForSingleObject(child.pi.hProcess, 5000);
        GetExitCodeProcess(child.pi.hProcess, &code);
        if (code == 77u) {
            SKIP("the harness found no presentable Vulkan device on this machine (exit 77)");
        }
        FAIL("the layer published no ring for the harness (harness exit " << code << ")");
    }

    // Keep it supervised while the harness presents, then read what it wrote.
    for (int i = 0; i < 50; ++i) {
        ++r.ctl->guardTicks;
        Sleep(100);
        if (WaitForSingleObject(child.pi.hProcess, 0) != WAIT_TIMEOUT) {
            break;
        }
    }
    WaitForSingleObject(child.pi.hProcess, 10000);
    DWORD code = 1;
    GetExitCodeProcess(child.pi.hProcess, &code);
    INFO("harness exit " << code << ", records " << r.st->writeIndex << ", status " << r.st->status);
    CHECK(code == 0u);
    CHECK(r.st->status == fl::FL_STATUS_READY);
    CHECK(r.st->writeIndex >= 20u);    // ~4 s at ~120/s minus device creation; a loose floor, not a rate claim
    CHECK((r.st->apiMask & (1u << fl::FL_API_VULKAN)) != 0u);
    CHECK(r.ring[0].api == static_cast<uint8_t>(fl::FL_API_VULKAN));
    CHECK(r.ring[0].outputW != 0u);
    CHECK((r.ring[0].measuredMask & fl::FL_MEASURED_OUTPUT_RES) != 0u);
    CHECK(r.st->faultCount == 0u);
}
