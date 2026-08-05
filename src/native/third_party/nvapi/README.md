# NVAPI (vendored)

NVIDIA NVAPI, copied from <https://github.com/NVIDIA/nvapi> at commit
`cd6918f60b3c9a0476fdfe7e89bb32330602049d` (2026-06-24), the `main` branch.

**Licence:** MIT, carried as an `SPDX-License-Identifier: MIT` block in every
header here and in `License.txt`. `tools/license-check.ps1` asserts both — the
per-file SPDX blocks, and that `legal/THIRD_PARTY_NOTICES.md` agrees with this
directory's existence **in both directions**.

**Unmodified, and that is checked rather than claimed.** Every file committed
here is byte-identical to upstream: `git hash-object` on the clone and the blob
in this repository's index agree, for the headers, `License.txt` and
`amd64/nvapi64.lib` alike. The repository is CRLF (`.gitattributes` sets
`* text=auto eol=crlf`) and upstream NVAPI is LF, but that is a **checkout**
transformation — the index stores LF, so the object under version control is the
upstream bytes. `*.lib binary` keeps the import library out of it entirely. This
matters beyond tidiness: "consumed unmodified" is what keeps a vendored MIT
dependency out of modification territory.

## Why that repository and not the SDK installer

This is the whole reason the licence question was answerable. `docs/spike-notes.md`
§0 (M3) records the measurement: `amd64/nvapi64.lib` is a **tracked file in the
MIT-licensed repository**, and `License.txt` opens

> `nvapi.lib and nvapi64.lib are licensed under the following terms:`

followed by `SPDX-License-Identifier: MIT`. The grant names the import libraries
as its subject, so the **binary** is covered and not merely the headers. A `.lib`
obtained from the NVIDIA SDK installer would have arrived under the SDK's own
agreement instead, and could not have been vendored into a GPL-3.0 repository.

MIT is one-way compatible with GPL-3.0. The obligation is retaining the notice,
which is what this file, `License.txt` and `legal/licenses/nvapi-MIT.txt` do.

## What is here, and what is not

`nvapi.h`'s complete include closure, plus the interface-id table:

```
nvapi.h                  nvapi_interface.h
nvapi_lite_salstart.h    nvapi_lite_common.h    nvapi_lite_sli.h
nvapi_lite_surround.h    nvapi_lite_stereo.h    nvapi_lite_d3dext.h
nvapi_lite_salend.h
amd64/nvapi64.lib        License.txt
```

**Not taken**, deliberately:

- `x86/nvapi.lib` — FrameLedger is x64 only (CLAUDE.md, NFR-8). A 32-bit import
  library is 438 KB of material we could never link and would still have to
  disclose.
- `NvApiDriverSettings.{c,h}` — driver-profile settings. Nothing here writes
  driver settings, and the `.c` would be swept into a source glob by anyone who
  added one.
- `nvHLSLExtns.h`, `nvHLSLExtnsInternal.h`, `nvShaderExtnEnums.h` — HLSL shader
  extensions, for shaders we do not author.

The runtime lives in `nvapi64.dll`, which ships with the user's installed NVIDIA
driver. **We never redistribute it**, and its use falls under GPLv3 §1's System
Library provision.

## How it is consumed

`fl_nvapi`, an `INTERFACE` target in `../CMakeLists.txt`, adds this directory as a
`SYSTEM` include and links `amd64/nvapi64.lib`. `SYSTEM` because these headers are
consumed **unmodified**: `/W4 /WX` must not fail our build on somebody else's
warnings, and patching a vendored header would drag it out of "consumed
unmodified" and into modification territory for no benefit.

`nvapi64.lib` is a **static** library of stubs that reach `nvapi64.dll` through
`nvapi_QueryInterface` at first call — not a normal import library. So linking it
does **not** make the DLL a load-time dependency: a machine with no NVIDIA driver
still loads our binaries, and `NvAPI_Initialize` returns an error instead.
`docs/18_GPU_VENDOR_APIS.md` §L3 requires exactly that degradation, and
`fl-probe-nvapi` is what proves it rather than assuming it.

## One thing the vendoring measured, which the design doc had wrong

`docs/18_GPU_VENDOR_APIS.md` §L3's function table named `NvAPI_GPU_GetMemoryInfo`.
In these headers that function carries

```
__nvapi_deprecated_function("Do not use this function - it is deprecated in
release 520. Instead, use NvAPI_GPU_GetMemoryInfoEx.")
```

Under `/W4 /WX` a call to it fails the native build. The table now names
`NvAPI_GPU_GetMemoryInfoEx`. Worth recording as the kind of thing vendoring finds
and reading a vendor's web documentation does not.
