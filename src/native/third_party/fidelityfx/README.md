# AMD FidelityFX SDK headers (vendored)

Copied from <https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK>, tag
**`v2.3.0`** (commit `60f4ea81909200d8542eca14dccb2628b763a9a3`, "AMD FSR SDK 2.3.0",
2026-06-24 — the `main` head on the day of vendoring, 2026-09-04). **Five headers and
the licence file. No `signedbin/`, no `.lib`, no `.cpp`, no `.hpp` helpers, and
deliberately not the whole `Kits/FidelityFX/` tree** — see §What is here, and what is not.

**Licence: MIT, by exception, and the exception is the whole tree.** Upstream has no
root `LICENSE`. Its `docs/license.md` opens with a binary-only, no-reverse-engineering
licence that *"applies to all files except as noted below"*, followed by an exception
list of **845** paths placed under the MIT grant. Read off the GitHub tree API on
2026-09-04: the repository at that commit holds **exactly 845 blobs, and every one of
them is on the list** — the signed effect DLLs included — so the default licence
governs no file that is actually in the tree. Every header vendored here is on the list
by path **and** carries the MIT grant inline in its own banner. `license.md` here is
upstream's file verbatim, exception list and all, and `tools/license-check.ps1` §2d
asserts both facts per file rather than trusting this paragraph: every path under
`Kits/` must appear on the list, and every `.h` must contain the grant text.

**Why this tag and not `v1.1.4`.** `docs/18_GPU_VENDOR_APIS.md` recorded on 2026-09-04
that `v1.1.4` (root `LICENSE.txt` = MIT) was the tag to take *"unless a title ships an
API version that needs 2.x"*. Titles installed on the dev machine already do: Hell Is Us
ships `amd_fidelityfx_upscaler_dx12.dll` 4.0.3 and `amd_fidelityfx_framegeneration_dx12.dll`
4.0.0; Expedition 33 and Dying Light: The Beast ship upscaler 4.0.2 and frame generation
3.1.5 — all SDK 2.x effect DLLs — and the SDK's own sample dispatches
`ffxDispatchDescFrameGenerationPrepareV2` (`0x0002000c`), which `v1.1.4` does not
declare. Every value and layout the Overlay reads is identical in both tags
(`FFX_API_DISPATCH_DESC_TYPE_UPSCALE 0x00010001`, `…_FRAMEGENERATION 0x00020003`,
`…_FRAMEGENERATION_PREPARE 0x00020004`, `ffxApiHeader`, `FfxApiResource`,
`ffxDispatchDescUpscale`, the `Prepare` descriptors' prefix through `renderSize`); 2.3.0
is a superset, so the SDK 1.1.x monolith (`amd_fidelityfx_dx12.dll` 1.0.1, which Lies of P,
Cyberpunk 2077 and Rune Factory ship) is decoded by the same constants.

**Unmodified, and checked rather than claimed.** `git hash-object` on each vendored file
equals the upstream blob sha at `v2.3.0`:

| | upstream blob |
|---|---|
| `Kits/FidelityFX/api/include/ffx_api.h` | `484ec23fa38a34c9061451cffd9f420ec2f7c1e8` |
| `Kits/FidelityFX/api/include/ffx_api_types.h` | `07c0da20c8ae370caaa8bc674e7f22df83cbf163` |
| `Kits/FidelityFX/upscalers/include/ffx_upscale.h` | `021ac23c179265752f5e6b66ef8487d8c12d17e9` |
| `Kits/FidelityFX/framegeneration/include/ffx_framegeneration.h` | `5ca5486460744e4357e30d3d273d7f12019fb5b1` |
| `Kits/FidelityFX/framegeneration/include/ffx_framegeneration_api_types.h` | `e7e514ddb6666753de3b14f720d8e6774339b0b9` |
| `license.md` (upstream `docs/license.md`) | `a80304f4edb2a86ae349e39872ea9c87a39c6f97` |

Upstream ships **LF**. `.gitattributes` (`* text=auto eol=crlf`) stores LF in the index
and materialises CRLF in the working tree, so `git ls-files -s` agrees with the table
above and a checked-out file differs from upstream in line endings only — the NVAPI
case, not the Streamline one.

## What is here, and what is not

The include closure of the three headers the Overlay reads, kept in **upstream's
directory layout** because the headers include each other by relative path
(`ffx_upscale.h` → `"../../api/include/ffx_api.h"`); flattening them would mean editing
them, which drags them out of "consumed unmodified".

| | |
|---|---|
| `api/include/ffx_api.h` | the five entry points and their `PfnFfx*` typedefs, `ffxApiHeader` (`type`, `pNext`), the effect ids and `FFX_API_MAKE_EFFECT_SUB_ID` |
| `api/include/ffx_api_types.h` | `FfxApiDimensions2D`, `FfxApiResource` and the other plain structs the descriptors are built from |
| `upscalers/include/ffx_upscale.h` | `FFX_API_DISPATCH_DESC_TYPE_UPSCALE` and `ffxDispatchDescUpscale` (`renderSize`, `upscaleSize`), `FfxApiUpscaleQualityMode` |
| `framegeneration/include/ffx_framegeneration.h` | `FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION`, `…_PREPARE`, `…_PREPARE_V2` and their descriptors (`frameID`, `renderSize`, `numGeneratedFrames`) |
| `framegeneration/include/ffx_framegeneration_api_types.h` | included by the header above |

Closure outside this directory: `<stdint.h>`, `<stdbool.h>`. **No STL, no `<windows.h>`,
no D3D headers** — verified on the upstream files — so the `-D_HAS_EXCEPTIONS=0` hazard
`third_party/streamline/README.md` records for `sl.h` does not arise here: there is
nothing in this closure that could throw.

**Excluded deliberately, and each exclusion is load-bearing:**

- **`Kits/FidelityFX/signedbin/`** — the effect DLLs and `amd_fidelityfx_loader_dx12.lib`.
  They are on the MIT list too, and it changes nothing: FrameLedger observes the copies
  the *game* ships and never loads, links or redistributes a vendor runtime
  (`legal/THIRD_PARTY_NOTICES.md`). `license-check.ps1` fails on any `.dll`/`.lib`/`.exe`
  under this directory.
- **`api/include/ffx_api_loader.h`** — a `GetProcAddress` helper that includes
  `<windows.h>`. The Overlay resolves symbols with its own module-scoped resolver and
  must never import a vendor module; a helper whose whole purpose is loading one is not
  something to have lying about.
- **`api/include/dx12/ffx_api_dx12.h`, `framegeneration/include/dx12/…`** — the backend
  and frame-generation-swapchain descriptors (`0x30001`, `0x30005`, `0x30006`). Nothing
  decodes them yet, and `docs/18_GPU_VENDOR_APIS.md` records that an unconsumed vendored
  header can have an incomplete closure with every gate green, so nothing is vendored
  ahead of the code that consumes it. They are MIT and on the list; take them with the
  PR that reads them.
- **The `.hpp` helpers, `api/internal/`, the provider sources, `Kits/Cauldron2/`,
  `Samples/`** — source and framework code, not declarations. MIT, and not ours to carry.

## How it is consumed, and the constraint that shapes that

Through the `fl_fidelityfx_headers` INTERFACE target, `SYSTEM`, with the three
`include/` directories on the path — the same treatment `fl_streamline_headers` gets,
and for the same reason: `/W4 /WX` must never fail our build on somebody else's
warnings, because patching a vendored header would drag it out of "consumed unmodified".

**For types only. Never link, and never take the address of a vendor function in
evaluated code.** `ffx_api.h` declares the five entry points as
`FFX_API_ENTRY` = `__declspec(dllexport)` — unconditionally, with no import/export
switch. A declaration with `dllexport` and no definition exports nothing, so including
the header in `FrameLedger.Overlay` is harmless *as long as nothing defines or references
those names there*; `tools/hookinventory-check.ps1` Pass C reads the built Overlay's
**export table as well as its import table** and fails on any `ffx*`-shaped name in
either. The stubs under `tools/vendor-stubs/` are the only place in this repository that
defines them, and they do so through the header's own `FFX_API_ENTRY`, so the fixture
and the detour cannot disagree about linkage or signature.

**One deprecation attribute, and where it bites.** `ffx_framegeneration.h` marks
`struct ffxDispatchDescFrameGenerationPrepare` `[[deprecated]]` in favour of `…PrepareV2`.
Under `/W4 /WX` naming the old struct is a build error. The two share their prefix through
`renderSize` byte for byte, so the Overlay reads a `PREPARE` (`0x00020004`) descriptor
through the `V2` layout and asserts that equality inside a scoped `#pragma warning` block
rather than suppressing the attribute globally — the SDK 1.1.x monolith still sends the
old type, so this is not theoretical.
