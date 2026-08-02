# Vulkan headers (vendored)

Khronos Vulkan headers, copied from the Vulkan SDK **1.4.357.0** installed on
the development machine (`docs/spike-notes.md` §Environment).

**Licence:** `Apache-2.0 OR MIT`, carried as an SPDX line in every file here.
Both are one-way compatible with GPL-3.0. The Apache-2.0 text ships in
`legal/licenses/apache-2.0.txt`, and `tools/license-check.ps1` fails the build
if it goes missing.

## Why vendored rather than fetched

MinHook arrives via `FetchContent` pinned to a commit, and the same could have
been done here. It was not, for two reasons:

1. **CI must not need the Vulkan SDK.** `FrameLedger.VkLayer` cannot compile
   without these headers, and the alternative was adding a ~1 GB SDK install
   step to every CI run. `.github/workflows/ci.yml` deliberately has no such
   step.
2. **The loader is what we are being compatible with.** These are the exact
   headers matching the loader (instance 1.4.357) that the blast-radius test in
   `docs/spike-notes.md` §2 runs against. Fetching a different revision would
   mean testing against one contract and compiling against another.

## What is here, and what is not

Only the **C** closure a Windows layer needs:

```
vulkan/vk_platform.h  vulkan/vulkan_core.h  vulkan/vulkan.h
vulkan/vulkan_win32.h vulkan/vk_layer.h     vk_video/*.h
```

The C++ bindings (`vulkan.hpp`, `vulkan_structs.hpp`, `vulkan_raii.hpp`, …) are
**excluded**: they are the bulk of the SDK's 28 MB, they allocate and throw, and
`CLAUDE.md` forbids both in the layer. `vk_icd.h` is excluded too — that is the
driver-side interface and we are not a driver.

## Modifying these

Don't. They are consumed unmodified, which is what keeps the licence position
simple. If a newer Vulkan version is needed, replace the whole set from a single
SDK install and record the version here — never patch a file in place.
