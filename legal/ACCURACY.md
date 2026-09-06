<!--
  THE ONE ACCURACY BLOCK. Embedded verbatim in README.md and legal/DISCLAIMER.md
  between the two HTML-comment markers named `accuracy-block:begin` and
  `accuracy-block:end` (spelled out in tools/accuracy-check.ps1 — not here, because a
  literal marker inside this comment would end the comment early), and that gate
  fails the build when any copy differs from this file
  (20_OPEN_QUESTIONS §S23-6: the accuracy blocks were hand-maintained prose that
  nothing verified, and DISCLAIMER's went stale four times in one direction and once
  in the other). Whether the sentences below are TRUE is still a human's job — the
  gate only makes there be one place to get them wrong. Re-read this file in the PR
  that changes what FrameLedger.Overlay or the capture host does; date the change.
  HTML comments are not part of the block.
-->
> ⚠ **What FrameLedger actually measures today — 2026-09-06.** The software is pre-alpha: the
> measurement path exists as an injected Direct3D 11/12 component and an **unshipped** capture
> host that drives the guard loop and prints a report; there is no Agent loop, no storage, no
> charts, no library import, no UI and no installer yet.
>
> - **Frame times and output resolution:** measured, from the present hook, for injected D3D11/12
>   titles. The Vulkan layer and OpenGL/D3D9 intercept nothing yet.
> - **Which upscaler is running:** measured from the API the game calls — DLSS (with Ray
>   Reconstruction Yes/No) through NVIDIA Streamline; FSR 2/3.x/4 through AMD's shipped DLLs; DLSS on
>   titles that bypass Streamline is reported from the NVIDIA driver's own per-process record,
>   labelled *driver-reported*. Intel XeSS is **not** read (its SDK licence forbids it) and reads
>   `N/A` by policy; an upscaler compiled into the game executable reads `N/A`.
> - **Quality preset:** `N/A` everywhere. No route this software may use exposes it.
> - **Render → output resolution:** measured where the vendor's own call carries the size (AMD
>   dispatches, some Streamline titles); `N/A` where it does not (NVIDIA-direct titles, most
>   Direct3D 12 Streamline titles).
> - **Frame generation:** the Displayed rate is counted from presents — including, on one title,
>   presents DXGI counted that the hook could not — and the Native rate from the vendor's own
>   per-frame calls where the title makes them. Identity: DLSS-G and FSR-FG named from the calls
>   the game makes; anything else (XeSS-FG, a generator compiled into the game) is reported *by
>   elimination* and never named. Where no per-frame call exists the Native rate is `N/A`.
> - **Ray tracing:** Yes/No measured from DXR dispatches and acceleration-structure builds on
>   Direct3D 12; the technique and path tracing are `N/A`.
> - **Not measured at all:** video memory, shader-compilation stutter, PC latency (Reflex), HDR.
> - **Safety:** every pre-injection check runs before injection, including the signed-by-a-known-
>   vendor half of the suspicious-module rule (since 2026-09-06), and the unshipped capture host
>   re-runs them every 30 s and stops the capture on refusal. No shipped component drives that loop
>   yet. There is no override anywhere.
>
> Where a value is not measured it reads `N/A`; the software never substitutes an estimate.
