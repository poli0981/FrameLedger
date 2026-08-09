# NVIDIA Streamline headers (vendored)

Copied from <https://github.com/NVIDIA-RTX/Streamline>, `main`, on 2026-08-09.
**Headers only. No `.lib`, no `.dll`, no source, and deliberately not the whole
`include/` directory** — see §What is here, and what is not.

**Licence:** MIT. `license.txt` is the upstream file, verbatim, and it opens with
the standard MIT permission grant. It carries **no** SPDX line — unlike NVAPI —
so `tools/license-check.ps1` asserts the grant text itself rather than an
identifier.

**Unmodified, and checked rather than claimed.** `git hash-object` on the
downloaded upstream file and `git ls-files -s` on this repository's index agree
for `sl.h`, `sl_core_api.h` and `sl_core_types.h`. Upstream ships **CRLF** and
the index blob is CRLF too, so unlike the NVAPI case — where upstream is LF and
`.gitattributes` normalisation was the thing that needed explaining — no
transformation happens here in either direction. The bytes under version control
are the upstream bytes.

## Why this could be vendored at all

`docs/18_GPU_VENDOR_APIS.md` §Checklist before adding any vendor SDK, step 1: MIT
⇒ *"proceed, add the licence copy, done"*. Verified against upstream on
2026-08-09 rather than assumed — `license.txt` contains none of step 2's four
needles (`solely for`, `indemnify`, `terminate`, `reverse engineer`), so step 3's
re-declaration prohibition is **unreachable**: it is the consequent of a licence
*failure*, not a rule about vendor APIs in general.

That distinction is the whole reason this directory exists, and it is worth
keeping written down, because the neighbouring case goes the other way. **NVIDIA
NGX / DLSS is the "NVIDIA RTX SDKs License"** and hits three needles outright —
*"defend, **indemnify** and hold harmless"*, *"may not **reverse engineer**,
decompile or disassemble"*, *"will **terminate** automatically without notice"*.
Step 3 therefore binds NGX: it may not be vendored **and may not be worked around
by re-declaring the API**. Streamline is not NGX, and the two must not be
conflated because `sl.common.dll` exports NGX-named symbols.

It also answers a question the repository had already recorded as open.
`docs/spike-notes.md` §5 says §H5 case 3 is *"blocked on a licence decision, not
on hardware"* — `slInit()`'s `sl::Preferences` argument being vendor ABI, with
the IGCL rule *"unanswered for NVIDIA"*. It is answered here.

## What is here, and what is not

The complete include closure of `sl.h`, and nothing else — nine files:

| | |
|---|---|
| `sl.h` | the umbrella header; includes the other eight |
| `sl_struct.h` `sl_result.h` `sl_version.h` `sl_consts.h` | base types, result codes, version, constants |
| `sl_appidentity.h` `sl_device_wrappers.h` | included by `sl.h`; **neither carries a licence header of its own** — both are covered by `license.txt` |
| `sl_core_api.h` | declares `slEvaluateFeature` and publishes `PFun_slEvaluateFeature` |
| `sl_core_types.h` | `using Feature = uint32_t`, the `kFeature*` ids, `BufferType` |

**Excluded deliberately, and each exclusion is load-bearing:**

- **`sl_nvperf.h`** — `license.txt` carries a *second, proprietary* block, *"NSight
  Perf SDK License, Version 2023.3"*, and names its subjects: `sl_nvperf.h` and
  `sl_nvperf.dll`. The MIT grant does not cover them. Same shape as the NVAPI
  vendoring excluding `x86/nvapi.lib`, for a different reason.
- **All of `external/`** — upstream's `external/ngx-sdk/` is the RTX SDKs Licence.
  A recursive copy does the forbidden thing by default, which is why this
  directory was assembled file by file from the closure rather than by copying a
  tree.
- **The fourteen other `include/` headers** (`sl_dlss.h`, `sl_dlss_g.h`,
  `sl_reflex.h`, `sl_helpers*.h`, …). `docs/18_GPU_VENDOR_APIS.md` records that an
  unconsumed vendored dependency can have an incomplete header closure with every
  gate green, so nothing is vendored ahead of the code that consumes it.
  `sl_dlss.h` lands with the PR that reads `DLSSOptions`; `sl_dlss_g.h` with
  frame generation.

## How it is consumed, and the constraint that shapes that

Through the `fl_streamline_headers` INTERFACE target, `SYSTEM`, so `/W4 /WX` never
fails our build on somebody else's warnings — the same treatment
`fl_vulkan_headers` gets, and for the same reason: patching a vendored header
would drag it out of "consumed unmodified".

**For types and `decltype` only. Never link, and never take the address of a
vendor function in evaluated code.** `SL_API` declarations such as
`slEvaluateFeature` are `extern "C"` imports; taking one's address, or adding
`target_link_libraries(... sl.interposer)`, would make `sl.interposer.dll` a
**load-time dependency of `FrameLedger.Overlay.dll`**. The Overlay would then
fail to load in every game that ships no Streamline — which is most of them —
with no FrameLedger message anywhere, because the failure happens in the loader
before any of our code runs. The Overlay resolves the symbol at runtime with
`GetProcAddress` on a module the game already loaded, exactly as it must.

`tools/hookinventory-check.ps1` asserts the second half of that: the built
`FrameLedger.Overlay.dll` must import nothing matching `^(sl\.|nvngx|libxess|ffx_|amd_fidelityfx)`.

**One consequence worth stating rather than discovering.** `sl_consts.h` and
`sl_version.h` include `<string>`, and `sl_core_types.h` includes `<vector>`, so
including `sl.h` pulls throwing STL into a translation unit the Overlay compiles
with `-D_HAS_EXCEPTIONS=0` — where MSVC rewrites a would-be throw to
`__fastfail(5)`, i.e. **it kills the host game, uncatchably, and SEH cannot
intercept it** (`docs/17_HOOK_ENGINE.md` §Build profile). Measured 2026-08-09:
the closure contains **no `throw`, `try` or `catch`**, and every `std::string` /
`std::vector` use in it is an inline or template member — `Version::toStr()`,
`getPrecisionFormulaAsStr()`, `copyFrom` / `copyTo` — which is never instantiated
unless called. So the cost today is compile time and nothing else. Calling one of
those helpers from a hook path would change that, and is the thing to look for in
review.
