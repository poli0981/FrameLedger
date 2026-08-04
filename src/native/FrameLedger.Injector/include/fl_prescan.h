// Check 4 — the static, pre-launch anti-cheat scan (docs/19_SAFETY §Pre-injection
// checks item 4, docs/05_DETECTION §Anti-cheat pre-scan).
//
// This check was DECLARED but never implemented. `Reason::kAntiCheatDirectory`
// and `kAntiCheatFile` existed, were named in ReasonName, and were mirrored into
// the managed enum — and nothing produced either, while two documents described
// the check as live and 14_TESTING specified a test for it. Three artifacts
// agreed on a behaviour no code had.
//
// Two rules shape this file:
//
//   1. IT RUNS INSIDE THE CHOKEPOINT. CheckStaticPreScan is called from
//      EvaluateImpl, not offered to a caller who might act on it. A pre-scan
//      that only advises the UI would be a check that gates nothing — and with
//      no persistence layer yet there is nowhere for such a verdict to be
//      stored, so `hook_blocked_reason` could not carry it either.
//   2. NO NEW MATCHING. Names are matched with fl::guard::MatchName against
//      Group::kDirectories and Group::kFiles — the same matcher, over the same
//      rules file, as the injection guard and the Vulkan layer. A second
//      matcher that can disagree with the first is a fail-open by construction
//      (§S15 item 1), and a family removed from the rules must stop firing
//      everywhere at once.

#ifndef FL_PRESCAN_H
#define FL_PRESCAN_H

#include <cstddef>
#include <fl_ac_rules.h>
#include <fl_guard.h>

namespace fl::guard {

// How far below the game directory we look, and how much we are willing to
// look at.
//
// Depth 2 was not arbitrary and was still too shallow. `EasyAntiCheat/` sits
// beside the executable and its EOS payloads sit one level inside it, which the
// old value covered — but "by 19_SAFETY's own table, deeper signals do not live
// there" was an assumption about the table, not about game installs.
//
// MEASURED 2026-08-04 (spike-notes §13): Neverness To Everness ships its own
// KERNEL DRIVER at `NTEGlobal/driver/PGameProtectDriver_X64.sys` — two
// directories below the install root, and therefore invisible. Adding the family
// to the blocklist changed nothing from the install root, which is where check 4
// actually runs; it only fired when the scan was started from `NTEGlobal`. A
// blocklist row that cannot be reached is coverage on paper.
//
// The cost was measured before the value moved, not assumed. Across 67 installed
// titles the worst case is 506 entries at the old reach and 729 at the new one,
// against kMaxPreScanEntries = 4096 — 18% of budget, 5.6x headroom. That matters
// because the entry cap is a REFUSAL: overrunning it does not scan less, it
// refuses the title.
//
// Both caps are REFUSALS, not truncations. A walk that stopped early has not
// seen the directory, and this file has no "scanned what we could" state.
inline constexpr std::size_t kMaxPreScanDepth = 3;
inline constexpr std::size_t kMaxPreScanEntries = 4096;

// Longest game directory path we will hold. MAX_PATH is not enough for a real
// Steam library on a long user name, and a truncated path is a path to somewhere
// else — so this is generous and overflow refuses.
inline constexpr std::size_t kMaxPreScanPathLen = 1024;

// Resolve a game's INSTALL ROOT from the directory its executable lives in.
//
// Not the same thing. Unreal puts the exe at <root>\<Project>\Binaries\Win64\,
// so scanning the executable's own directory looks at a folder containing the
// shipping binary and nothing else — and `EasyAntiCheat/` sits at the install
// root. MEASURED on Lies of P (2026-08-03): the exe is three levels below the
// root, and the pre-scan saw seven files, none of which could ever have been an
// anti-cheat SDK. For exactly the layout most likely to carry EAC, check 4 was
// looking in the wrong place.
//
// Walks up to a hardcoded platform boundary (`steamapps\common\<X>` and
// friends) and returns that game's folder. Boundaries are hardcoded for the same
// reason IsPlatformLauncher is: a data-driven boundary would let a rules update
// move where the hard gate looks.
//
// WHEN NO BOUNDARY IS RECOGNISED, `exeDir` IS RETURNED UNCHANGED. Walking up
// blindly is worse than staying put — one level above a game installed loose in
// `D:\games\Title\` is a folder of other games, and refusing this title because
// a sibling ships anti-cheat is a false refusal with no appeal.
//
// Returns false if the result does not fit, which the caller treats as
// "cannot determine".
[[nodiscard]] bool ResolveInstallRoot(const wchar_t* exeDir, wchar_t* out, std::size_t cap) noexcept;

// Check 4, as EvaluateImpl runs it: derive the target's directory from its pid,
// then scan it.
//
// Takes Sources because every evidence input in this guard is a seam — an input
// whose failure path cannot be exercised is an input whose failure path is
// unverified (fl_guard.h rule 3). It grants nothing: it opens no process with
// injection rights, reaches no injection primitive, and returns a Verdict its
// only caller already had to reach anyway.
[[nodiscard]] Verdict CheckStaticPreScan(const Sources& s, const Rules& rules, std::uint32_t targetPid) noexcept;

// The advisory form, for the UI's pre-launch question "can this game be
// enabled at all?" (FR-2.2). Same matcher, same polarity, no pid.
//
// Loads the rules itself and uses SystemSources(), so a caller cannot choose the
// evidence — the same reason Evaluate and GuardedInject take no Sources.
[[nodiscard]] Verdict StaticPreScan(const wchar_t* gameDirectory) noexcept;

#ifdef FL_GUARD_TESTABLE
// TEST-ONLY, exactly as fl_guard.h defines the term: compiled out of everything
// that ships, and only src/native/tests may define the macro.
[[nodiscard]] Verdict StaticPreScanWithSources(const wchar_t* gameDirectory, const Sources& sources) noexcept;
#endif

}    // namespace fl::guard

#endif    // FL_PRESCAN_H
