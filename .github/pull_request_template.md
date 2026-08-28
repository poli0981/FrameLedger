<!--
Definition of done is CLAUDE.md §Definition of done. This template is that
list, so it is checked rather than remembered.
-->

## What and why

<!-- What changed, and which doc or issue drove it. -->

## Definition of done

- [ ] Builds warning-free (C# **and** C++); tests green
- [ ] `dotnet format` and `clang-format` clean (`./build.ps1 check`)
- [ ] Any deviation from a doc updates that doc **in this PR**
- [ ] New user-visible strings exist in `en`, `vi` and `ja`

## Capture-layer changes only

Skip this section if the PR does not touch the Overlay, the injector, the
Vulkan layer, or the guard. Otherwise every box must be ticked — these are the
review questions from `docs/19_SAFETY_AND_ANTICHEAT.md` §Review checklist.

- [ ] No hook reads anything beyond the arguments of the API it hooks (CLAUDE.md rule 4)
- [ ] Nothing here makes FrameLedger harder for anti-cheat to identify (rule 3)
- [ ] No new path can reach `Injector.Attach` without a passing guard result (rule 2)
- [ ] Every new hook is listed in `docs/17_HOOK_ENGINE.md` §Hook inventory with a stated purpose
- [ ] No hook body allocates, locks, logs, or throws (rule 5)
- [ ] An unavailable hook degrades to Tier 2 — which measures NOTHING — and the session records why, rather than reporting a zero or a partial number
- [ ] Measured hook overhead is stated below (`docs/14_TESTING.md` §Hook overhead)

**Measured overhead:** <!-- e.g. "0.31 µs/present, game Avg FPS delta -0.2%" -->

## Safety-string changes only

- [ ] `Safety_*` resx keys have human-reviewed `ja` (never machine-drafted — `docs/09_I18N.md`)
- [ ] Wording was not softened in any locale

## Notes for the reviewer

<!-- Anything that would be hard to see from the diff. If you resolved an item
     in docs/20_OPEN_QUESTIONS.md, say which one and delete it from that file. -->
