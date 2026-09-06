// hook-harness --vulkan --hold-presenting N: a REAL Vulkan swapchain on a hidden
// window, presented every few milliseconds for N seconds, so the layer's
// vkQueuePresentKHR can be exercised under the real loader (12_BUILD §Targets:
// "Vulkan and OpenGL modes to follow as those hooks land" -- this is the Vulkan
// one, P1 item 3).
//
// The loader is reached through LoadLibraryW("vulkan-1.dll") and
// vkGetInstanceProcAddr, never a link-time import: the repository vendors the
// Khronos HEADERS only, and CI has no loader at all. Exit codes:
//   0   presented for the whole hold
//   77  this machine cannot run the fixture (no loader, no device, no surface
//       support) -- a SKIP for the test that spawns it, never a failure
//   2   the fixture is broken (an API that should have worked did not)

#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>

#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

PFN_vkGetInstanceProcAddr g_gipa = nullptr;

template <typename T>
T Instance(VkInstance instance, const char* name) {
    return reinterpret_cast<T>(g_gipa(instance, name));
}

LRESULT CALLBACK HiddenWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return DefWindowProcW(h, m, w, l);
}

HWND CreateHiddenWindow() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = &HiddenWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FrameLedgerHookHarnessVulkan";
    RegisterClassW(&wc);
    // Never shown: a surface needs an HWND, not a visible one.
    return CreateWindowExW(0, wc.lpszClassName, L"FrameLedger hook-harness (Vulkan)", WS_OVERLAPPEDWINDOW, 0, 0, 256,
                           256, nullptr, nullptr, wc.hInstance, nullptr);
}

int Skip(const char* why) {
    std::printf("  [SKIP] %s\n", why);
    std::fflush(stdout);
    return 77;
}

int Broken(const char* what, VkResult r) {
    std::printf("  [FAIL] %s (VkResult %d)\n", what, static_cast<int>(r));
    std::fflush(stdout);
    return 2;
}

}    // namespace

int HoldPresentingVulkan(int seconds, int presentIntervalMs) {
    std::printf("\n[vulkan-hold] a real swapchain on a hidden window, presenting for %d second(s) every %d ms\n",
                seconds, presentIntervalMs);
    std::fflush(stdout);

    const HMODULE loader = LoadLibraryW(L"vulkan-1.dll");
    if (loader == nullptr) {
        return Skip("no vulkan-1.dll on this machine");
    }
    g_gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader, "vkGetInstanceProcAddr"));
    if (g_gipa == nullptr) {
        return Skip("vulkan-1.dll exports no vkGetInstanceProcAddr");
    }

    // --- instance ---------------------------------------------------------
    auto              createInstance = Instance<PFN_vkCreateInstance>(nullptr, "vkCreateInstance");
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "FrameLedger hook-harness";
    app.apiVersion = VK_API_VERSION_1_1;
    const char*          instanceExts[] = {"VK_KHR_surface", "VK_KHR_win32_surface"};
    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = instanceExts;
    VkInstance instance = nullptr;
    if (createInstance == nullptr || createInstance(&ici, nullptr, &instance) != VK_SUCCESS) {
        return Skip("vkCreateInstance failed (no ICD, or no surface extensions)");
    }

    auto enumPhys = Instance<PFN_vkEnumeratePhysicalDevices>(instance, "vkEnumeratePhysicalDevices");
    auto queueProps =
        Instance<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    auto surfaceSupport =
        Instance<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(instance, "vkGetPhysicalDeviceSurfaceSupportKHR");
    auto createSurface = Instance<PFN_vkCreateWin32SurfaceKHR>(instance, "vkCreateWin32SurfaceKHR");
    auto surfaceCaps =
        Instance<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    auto surfaceFormats =
        Instance<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    auto createDevice = Instance<PFN_vkCreateDevice>(instance, "vkCreateDevice");
    auto gdpa = Instance<PFN_vkGetDeviceProcAddr>(instance, "vkGetDeviceProcAddr");
    if (enumPhys == nullptr || queueProps == nullptr || surfaceSupport == nullptr || createSurface == nullptr ||
        surfaceCaps == nullptr || surfaceFormats == nullptr || createDevice == nullptr || gdpa == nullptr) {
        return Skip("the instance lacks the surface / device entry points this fixture needs");
    }

    // --- surface ----------------------------------------------------------
    const HWND hwnd = CreateHiddenWindow();
    if (hwnd == nullptr) {
        return Skip("no window could be created (no interactive window station?)");
    }
    VkWin32SurfaceCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    sci.hinstance = GetModuleHandleW(nullptr);
    sci.hwnd = hwnd;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (createSurface(instance, &sci, nullptr, &surface) != VK_SUCCESS) {
        return Skip("vkCreateWin32SurfaceKHR failed");
    }

    // --- a physical device with a queue that can draw AND present ---------
    uint32_t physCount = 0;
    enumPhys(instance, &physCount, nullptr);
    std::vector<VkPhysicalDevice> phys(physCount);
    enumPhys(instance, &physCount, phys.data());
    VkPhysicalDevice chosen = nullptr;
    uint32_t         family = 0;
    for (VkPhysicalDevice p : phys) {
        uint32_t n = 0;
        queueProps(p, &n, nullptr);
        std::vector<VkQueueFamilyProperties> props(n);
        queueProps(p, &n, props.data());
        for (uint32_t i = 0; i < n && chosen == nullptr; ++i) {
            VkBool32 present = VK_FALSE;
            if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u &&
                surfaceSupport(p, i, surface, &present) == VK_SUCCESS && present == VK_TRUE) {
                chosen = p;
                family = i;
            }
        }
        if (chosen != nullptr) {
            break;
        }
    }
    if (chosen == nullptr) {
        return Skip("no physical device with a graphics queue that can present to a Win32 surface");
    }

    // --- device -----------------------------------------------------------
    const float             priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    const char*        deviceExts[] = {"VK_KHR_swapchain"};
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = deviceExts;
    VkDevice device = nullptr;
    if (VkResult r = createDevice(chosen, &dci, nullptr, &device); r != VK_SUCCESS) {
        return Broken("vkCreateDevice", r);
    }
#define FL_DEV(name) auto name = reinterpret_cast<PFN_##name>(gdpa(device, #name))
    FL_DEV(vkGetDeviceQueue);
    FL_DEV(vkCreateSwapchainKHR);
    FL_DEV(vkGetSwapchainImagesKHR);
    FL_DEV(vkAcquireNextImageKHR);
    FL_DEV(vkQueuePresentKHR);
    FL_DEV(vkQueueSubmit);
    FL_DEV(vkQueueWaitIdle);
    FL_DEV(vkCreateSemaphore);
    FL_DEV(vkCreateCommandPool);
    FL_DEV(vkAllocateCommandBuffers);
    FL_DEV(vkBeginCommandBuffer);
    FL_DEV(vkEndCommandBuffer);
    FL_DEV(vkCmdPipelineBarrier);
    FL_DEV(vkDeviceWaitIdle);
#undef FL_DEV
    if (vkGetDeviceQueue == nullptr || vkCreateSwapchainKHR == nullptr || vkGetSwapchainImagesKHR == nullptr ||
        vkAcquireNextImageKHR == nullptr || vkQueuePresentKHR == nullptr || vkQueueSubmit == nullptr ||
        vkQueueWaitIdle == nullptr || vkCreateSemaphore == nullptr || vkCreateCommandPool == nullptr ||
        vkAllocateCommandBuffers == nullptr || vkBeginCommandBuffer == nullptr || vkEndCommandBuffer == nullptr ||
        vkCmdPipelineBarrier == nullptr || vkDeviceWaitIdle == nullptr) {
        return Broken("a device entry point was missing", VK_ERROR_INITIALIZATION_FAILED);
    }
    VkQueue queue = nullptr;
    vkGetDeviceQueue(device, family, 0, &queue);

    // --- swapchain --------------------------------------------------------
    VkSurfaceCapabilitiesKHR caps{};
    surfaceCaps(chosen, surface, &caps);
    uint32_t formatCount = 0;
    surfaceFormats(chosen, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    surfaceFormats(chosen, surface, &formatCount, formats.data());
    if (formats.empty()) {
        return Skip("the surface reports no formats");
    }
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu || extent.width == 0u || extent.height == 0u) {
        extent = VkExtent2D{256u, 256u};
    }
    VkSwapchainCreateInfoKHR sc{};
    sc.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sc.surface = surface;
    sc.minImageCount = caps.minImageCount < 2u ? 2u : caps.minImageCount;
    if (caps.maxImageCount != 0u && sc.minImageCount > caps.maxImageCount) {
        sc.minImageCount = caps.maxImageCount;
    }
    sc.imageFormat = formats[0].format;
    sc.imageColorSpace = formats[0].colorSpace;
    sc.imageExtent = extent;
    sc.imageArrayLayers = 1;
    sc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sc.preTransform = caps.currentTransform;
    sc.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0u
                            ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                            : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    sc.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sc.clipped = VK_TRUE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    if (VkResult r = vkCreateSwapchainKHR(device, &sc, nullptr, &swapchain); r != VK_SUCCESS) {
        return Broken("vkCreateSwapchainKHR", r);
    }
    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    std::vector<VkImage> images(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, images.data());

    // --- one command buffer per image: UNDEFINED -> PRESENT_SRC, nothing drawn -
    VkCommandPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = family;
    VkCommandPool pool = VK_NULL_HANDLE;
    if (VkResult r = vkCreateCommandPool(device, &pci, nullptr, &pool); r != VK_SUCCESS) {
        return Broken("vkCreateCommandPool", r);
    }
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = imageCount;
    std::vector<VkCommandBuffer> cmds(imageCount);
    if (VkResult r = vkAllocateCommandBuffers(device, &cai, cmds.data()); r != VK_SUCCESS) {
        return Broken("vkAllocateCommandBuffers", r);
    }
    for (uint32_t i = 0; i < imageCount; ++i) {
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmds[i], &bi);
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = images[i];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        vkCmdPipelineBarrier(cmds[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &barrier);
        vkEndCommandBuffer(cmds[i]);
    }
    VkSemaphoreCreateInfo semi{};
    semi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkSemaphore ready = VK_NULL_HANDLE;
    vkCreateSemaphore(device, &semi, nullptr, &acquired);
    vkCreateSemaphore(device, &semi, nullptr, &ready);

    // --- the hold ---------------------------------------------------------
    const ULONGLONG until = GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000ULL;
    long long       presented = 0;
    while (GetTickCount64() < until) {
        uint32_t index = 0;
        VkResult r = vkAcquireNextImageKHR(device, swapchain, 1000000000ull, acquired, VK_NULL_HANDLE, &index);
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
            return Broken("vkAcquireNextImageKHR", r);
        }
        const VkPipelineStageFlags wait = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkSubmitInfo               si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &acquired;
        si.pWaitDstStageMask = &wait;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmds[index];
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &ready;
        if (r = vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE); r != VK_SUCCESS) {
            return Broken("vkQueueSubmit", r);
        }
        VkPresentInfoKHR pi{};
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &ready;
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchain;
        pi.pImageIndices = &index;
        r = vkQueuePresentKHR(queue, &pi);
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
            return Broken("vkQueuePresentKHR", r);
        }
        ++presented;
        vkQueueWaitIdle(queue);    // simplest correct pacing: no fences to juggle in a fixture
        if (presentIntervalMs > 0) {
            Sleep(static_cast<DWORD>(presentIntervalMs));
        }
    }
    vkDeviceWaitIdle(device);
    // The count goes to stdout so a test can compare it against records drained
    // from the ring rather than asserting "more than zero".
    std::printf("  presented=%lld\n", presented);
    std::fflush(stdout);
    // Objects are left for process exit: this is a fixture that ends here, and a
    // teardown that itself could fail would only add exit codes to a test.
    return 0;
}
