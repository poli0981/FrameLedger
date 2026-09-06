// The anti-cheat guard — the hard gate (docs/19_SAFETY_AND_ANTICHEAT.md).
//
// This is the one component where a bug can cost somebody their account
// (docs/14_TESTING.md §Safety-guard tests), and the whole file is shaped by
// three rules that are not negotiable:
//
//   1. EVERY uncertainty is a REFUSAL. "I could not look" and "I looked and it
//      was clean" must never produce the same value. The project has already
//      shipped one defect of exactly that shape — EnumDeviceDrivers returning
//      258 drivers and zero usable names to a standard user, which a guard
//      would have read as "no anti-cheat present" on a machine running
//      Vanguard (docs/spike-notes.md §1).
//   2. NO CLEARANCE ESCAPES. The guard owns the chokepoint: it collects
//      evidence, matches, and calls the injection primitive itself. A token
//      handed to a caller can be ignored — the caller can simply not ask for
//      one. A symbol that does not exist cannot be called (§S8, §S13(b)).
//   3. EVERY INPUT IS A SEAM. Module enumeration, driver enumeration, service
//      queries, the process tree and the rules file all arrive through
//      function pointers, so the Catch2 suite can force each failure the
//      matrix in 14_TESTING requires. An input that cannot be made to fail in
//      a test is an input whose failure path is unverified.
//
// There is deliberately no "check and tell me the answer" public entry point.
// See FlGuardedInject at the bottom.

#ifndef FL_GUARD_H
#define FL_GUARD_H

#include <cstddef>
#include <cstdint>

namespace fl::guard {

// ---------------------------------------------------------------------------
// Verdict
// ---------------------------------------------------------------------------

// Why the guard refused. One value per pre-injection check and per failure
// class, because "refused" alone is not something the UI can explain and
// 19_SAFETY requires the user be told which check fired.
//
// Ordering note: kAllow is 0 ONLY so that a default-constructed Verdict is
// obviously wrong to a reader — it is never produced by the guard except on the
// one path that has actually passed every check. If that ever feels risky,
// change it; nothing depends on the numeric values.
enum class Reason : std::uint8_t {
    kAllow = 0,

    // Check 1 — target module scan.
    kBlockedModule,
    kModuleScanFailed,          // enumeration failed: cannot determine => refuse
    kProcessUnreadable,         // OpenProcess denied: cannot determine => refuse
    kProcessTreeUnavailable,    // could not establish the scan set (§S16)

    // Check 2 — machine-wide driver scan.
    kBlockedDriver,
    kDriverScanFailed,

    // Check 2b — services.
    kBlockedService,
    kServiceQueryFailed,    // DENIED, not ABSENT: cannot determine => refuse

    // Check 3 — per-title rules.
    kBlockedExecutable,
    kBlockedStoreId,

    // Check 4 — static, pre-launch.
    kAntiCheatDirectory,
    kAntiCheatFile,

    // The unknown-but-suspicious heuristic.
    kSuspiciousUnsigned,

    // The rules data itself.
    kRulesUnreadable,
    kRulesMalformed,
    kRulesIncomplete,    // parsed, but a required family is missing

    // Check 4's "cannot determine": the game directory was absent, unlistable,
    // truncated by a bound, or crossed a reparse point we will not follow.
    //
    // APPENDED, not slotted in beside kAntiCheatFile where it belongs
    // logically. These values cross a C ABI into the managed mirror, so
    // inserting one renumbers every reason after it — the whole tail would
    // shift by one and every stored or logged value would mean something else.
    // Grouping is worth less than a number that never moves.
    kPreScanFailed,

    // The guard PASSED and the injection still did not happen.
    //
    // These exist because the previous code returned kAllow with the truth in a
    // free-text signal, above a comment claiming "the caller distinguishes them
    // by reason" — and there was no reason to distinguish by. A caller reading
    // Allowed() got `true` for an injection that never occurred. Measured
    // 2026-08-03 against a real 32-bit title.
    //
    // Allowed() is now false for both: it means "the DLL is loaded in the
    // target", which is the only reading a caller can act on safely. The reason
    // says whose fault it was, because the responses differ — a refusal is
    // permanent, a failed injection may be worth retrying, and WOW64 is
    // permanent but for an entirely different reason.
    kInjectionFailed,

    // The target is a 32-bit process. Permanent and expected, not an error:
    // the Overlay is x64-only, so an x64 DLL cannot load there
    // (20_OPEN_QUESTIONS §Scope decisions). The UI should say so and offer
    // Tier 2 rather than reporting a failure the user could act on. (Tier 2
    // measures nothing since 2026-08-28; it is the honest record of a capture
    // that did not happen, not a lesser one.)
    kTargetIsWow64,

    // The PAYLOAD is not ours (§S22).
    //
    // Every other reason in this enum is about the TARGET or about the machine.
    // This one is about the DLL we were asked to load, and it exists because the
    // gate had a hole exactly that shape: FlGuardedInject takes a caller-supplied
    // path and, until this reason existed, the only thing asked of it was
    // GetFileAttributesW — "exists and is not a directory". A caller could hand
    // the shipped, exported guard any DLL on the machine and have it loaded into
    // any x64 process that happened to carry no anti-cheat.
    //
    // Refusing here is not a failed injection: nothing was attempted. It is the
    // guard declining, which is why it is a Reason and not a kInjectionFailed
    // variant — a mislabelled reason is a wrong answer for whoever has to fix it.
    kPayloadNotOurs,

    // Refusals the CAPTURE GATE makes, which this guard structurally cannot.
    //
    // Consent and per-game enablement are records of something a human did;
    // they live in the Agent's database and the native guard never sees them.
    // They are nevertheless reasons a Tier-1 capture was refused, and they
    // belong in THIS enum for the reason FlStaticPreScan already reports
    // through FlGuardResult: one reason table, one mirror surface, one place
    // the UI maps to a string.
    //
    // Before these existed the managed gate returned kBlockedExecutable for all
    // three — check 3's code, which at the time the native guard could not
    // produce at all (§S14: the matchers had no call site; #52 gave the
    // executable half one, so the native guard DOES produce it now, which makes
    // keeping these three distinct more important rather than less: a logged
    // kBlockedExecutable is now a real check-3 hit and must not be ambiguous
    // with a consent failure). So the UI would have told a user
    // "this title is on the per-title blocklist" when the truth was "you have
    // not accepted the consent dialog", and a logged kBlockedExecutable meant
    // one of four unrelated things.
    //
    // The guard itself never returns these. That is not a defect: the enum is
    // the vocabulary of the whole gate path, and it already carries
    // kInjectionFailed and kTargetIsWow64, which are not blocklist matches
    // either.
    kHookNotEnabled,
    kConsentMissing,
    kPreviouslyBlocked,

    // LAUNCH MODE (P1 item 2; 20_OPEN_QUESTIONS §S1 / §S13(c)): the guard was asked
    // to inject AS SOON AS the target had mapped a presentation runtime, and it
    // never did. A CREATE_SUSPENDED target has loaded nothing and
    // EnumProcessModulesEx fails against it with ERROR_PARTIAL_COPY (§S1, measured),
    // so launch mode cannot be create -> guard -> inject -> resume. It is create ->
    // resume -> POLL until dxgi+d3d11/d3d12, opengl32 or vulkan-1 is mapped -> the
    // full guard -> inject. These two are the poll's own ends: the process exited
    // first (a launcher that spawned the real game and quit -- the Agent's election,
    // P2 -- or a crash), or the budget ran out with no runtime mapped (a 2D title, or
    // a launcher still sitting on its window). Nothing was injected in either case.
    kLaunchTargetExited,
    kLaunchNoPresentationRuntime,

    // NOT A REASON. The count, so appending above it updates the exported
    // FlGuardReasonCount by construction.
    //
    // This replaces a static_assert that could not fire on the change it existed
    // to catch: it pinned kRulesIncomplete == 16, and kRulesIncomplete was the
    // LAST enumerator, so appending a reason left it at 16, the assert passed,
    // the exported count stayed at 17, and the managed mirror test iterated 0-16
    // and never compared the new value at all. Deriving the count removes the
    // need for anyone to remember.
    kCount,
};

// A refusal carries the signal that produced it, so the UI can say
// "EasyAntiCheat.dll was loaded in the target" rather than "blocked".
// Fixed-size: the guard allocates nothing.
struct Verdict {
    Reason reason = Reason::kRulesUnreadable;    // fail-closed default
    char   family[64] = {};                      // e.g. "Easy Anti-Cheat"
    char   signal[260] = {};                     // e.g. "EasyAntiCheat_EOS.dll"

    [[nodiscard]] bool Allowed() const noexcept { return reason == Reason::kAllow; }
};

// The literal default must be a refusal, not an allow. Asserted rather than
// trusted, because a future edit to the enum could silently invert it.
static_assert(static_cast<int>(Reason::kAllow) == 0, "kAllow must stay 0 for the Allowed() check");

// ---------------------------------------------------------------------------
// Evidence sources — the seams (rule 3 above)
// ---------------------------------------------------------------------------

// Every collector returns a tri-state, never a bare list. A bare empty list is
// the exact ambiguity that produced this project's worst defect: it reads as
// "nothing found" when it may mean "could not look".
enum class Collected : std::uint8_t {
    kOk = 0,
    kFailed,        // the call failed outright
    kIncomplete,    // partial result: e.g. WOW64 without LIST_MODULES_ALL
};

// Callbacks receive (context, name) for each item found. Returning false stops
// enumeration early — used when a match has already been found.
using NameSink = bool (*)(void* ctx, const char* name);

// As NameSink, plus where the module was loaded FROM.
//
// Modules get their own sink because §S22(b) needs a question the base name
// cannot answer: when a module trips the fuzzy name-fragment tier, is that
// module OURS? Keying the exemption on the base name would be spoofable by any
// DLL that borrows the name; keying it on the owning PROCESS — which is what
// §S18 originally did — asks about the wrong thing entirely and refused every
// FrameLedger host that did not sit beside FrameLedger.Guard.dll.
//
// `path` is nullptr when it could not be determined. That is NOT an enumeration
// failure — the base name is still enough to match the blocklist, so the scan
// continues — but a module we cannot locate is a module we cannot exempt, and
// the sink must treat null as "not ours".
using ModuleSink = bool (*)(void* ctx, const char* name, const wchar_t* path);

// As NameSink, plus whether the entry is a directory. Check 4 matches
// directories and files against different blocklist groups, and guessing from
// the name would be a second, weaker classifier.
using DirEntrySink = bool (*)(void* ctx, const char* name, bool isDirectory);

struct Sources {
    // Loaded modules of one process, by base name and load path.
    Collected (*EnumerateModules)(std::uint32_t pid, ModuleSink sink, void* ctx) = nullptr;

    // Machine-wide loaded kernel drivers, by full native path.
    Collected (*EnumerateDrivers)(NameSink sink, void* ctx) = nullptr;

    // A service by name. kOk + present=false means it is not RUNNING — either
    // absent (1060) or installed and stopped. kFailed means DENIED or anything
    // else, which is "cannot determine" and refuses. That distinction is the
    // whole reason this is not a bool (docs/19_SAFETY §Pre-injection checks
    // item 2).
    //
    // `present` means RUNNING, not installed. Measured: EasyAntiCheat_EOS is
    // installed machine-wide by any EOS title and sits Stopped/Manual, so the
    // installed-means-present reading refused every process on the machine.
    Collected (*QueryService)(const char* name, bool* present) = nullptr;

    // The scan set for §S16: the injection target, its descendants, and its
    // ancestors up to but excluding the first known platform launcher.
    Collected (*EnumerateScanSet)(std::uint32_t targetPid, bool (*sink)(void*, std::uint32_t), void* ctx) = nullptr;

    // Whole rules file into a caller-owned buffer. Returns bytes written, or
    // SIZE_MAX on any failure — unreadable, absent, or larger than the cap.
    std::size_t (*ReadRulesFile)(char* buffer, std::size_t cap) = nullptr;

    // Check 4 — the static pre-scan.
    //
    // The directory the target's image lives in. kFailed means we could not
    // name it, which refuses: check 4 cannot run against a directory we cannot
    // find, and "we did not look" has never been a pass in this file.
    Collected (*ImageDirectory)(std::uint32_t pid, wchar_t* out, std::size_t cap) = nullptr;

    // The target's executable LEAF NAME, for check 3. A separate seam from
    // ImageDirectory because it is a different fact: that one deliberately
    // resolves the INSTALL ROOT (Unreal puts the exe three levels below it), and
    // the name is exactly what it throws away.
    //
    // NARROW on purpose. `blockedExecutables` is ASCII by schema and
    // MatchesBlockedExecutable compares ASCII, so converting here keeps one
    // encoding boundary instead of two. The impl refuses a lossy conversion
    // rather than matching a mangled name -- §S21's ANSI defect was exactly a
    // silent lossy conversion, and a `?` in a name matches nothing while looking
    // like it looked.
    //
    // kFailed refuses (kProcessUnreadable). A target whose image we cannot name
    // is a target whose identity we do not know, and 19_SAFETY's check 3 note is
    // explicit that an unresolvable identity must read as UNKNOWN, never clean.
    Collected (*ImageFileName)(std::uint32_t pid, char* out, std::size_t cap) = nullptr;

    // Entries at and below `dir`, flattened, bounded by the caps in
    // fl_prescan.h. `isDirectory` is what decides which blocklist group the
    // name is matched against, so it is part of the evidence rather than
    // something the caller infers from the string.
    Collected (*EnumerateDirEntries)(const wchar_t* dir, DirEntrySink sink, void* ctx) = nullptr;

    // §S18/§S22(b) — is this MODULE one of OUR OWN binaries?
    //
    // The single narrow exception in this gate, and the only thing it may ever
    // do is suppress the FUZZY name-fragment tier for a module that is ours,
    // inside a scan-set process that is not the injection target. The exact
    // blocklist still applies in full, and every other refusal is untouched.
    //
    // Why it exists: FrameLedger.Guard.dll contains the substring `guard`, one
    // of the heuristic's nameFragments, and the project ships unsigned
    // (CLAUDE.md rule 9) so the signer half can never rescue it. In launch mode
    // the Agent is the game's parent and therefore inside the §S16 scan set, so
    // every launch-mode injection refused — and Vulkan Tier 1 with it, since the
    // layer's only enable path runs through launch mode.
    //
    // IT ASKS ABOUT THE MODULE, NOT THE PROCESS, and §S22(b) is the record of why
    // the process form was wrong. §S18 keyed this on the scan-set process's image
    // directory, which made the exemption depend on WHERE THE HOST LIVES rather
    // than on whose DLL tripped the tier: measured, the same binary run from
    // beside FrameLedger.Guard.dll returned Allow and run from anywhere else
    // returned SuspiciousUnsigned naming our own DLL. The Agent only worked
    // because a .targets file happens to put the DLL beside it. The module form
    // is also strictly NARROWER: a genuinely foreign suspicious module loaded
    // into a FrameLedger process — an AppInit DLL, an AV user-mode hook, an IME —
    // used to be suppressed along with our own, and now is not.
    //
    // kFailed means CANNOT DETERMINE, which means DO NOT SUPPRESS. The polarity
    // is the opposite of every other seam here — everywhere else "could not look"
    // refuses, and here it also refuses, because refusing IS the unsuppressed
    // answer. A seam that fails must never widen what is allowed.
    //
    // A tri-state plus an out-param rather than a bool, for the same reason
    // QueryService is: "no" and "could not tell" are different answers and the
    // caller has to be able to distinguish them.
    Collected (*ModuleIsOurOwn)(const wchar_t* modulePath, bool* isOurs) = nullptr;

    // §S19(b) — WHO SIGNED this module, from its EMBEDDED Authenticode signature,
    // verified OFFLINE (row G1, read 2026-09-06 after the CI leg and the owner's
    // adapters-disabled leg both left every verdict unchanged).
    //
    // kOk writes the signing certificate's subject `O=` into `out` — the field
    // 19_SAFETY §Blocklist seed names, measured on drivers, launchers and a NuGet
    // assembly whose CN changed between copies while its O= did not. The guard
    // then pairs it with the fragment tier: a fragment-matching module signed by
    // an organisation on the TRUSTED list keeps the scan looking; anything else
    // latches SuspiciousUnsigned, which is now literally what it says.
    //
    // kFailed is every other outcome and MEANS NOT TRUSTED: no embedded signature
    // (the catalog-signed system binaries — mskeyprotect.dll — land here by
    // design, the CryptCATAdmin* half being deferred with its own rationale), an
    // invalid one, a certificate whose O= could not be read, a buffer too small,
    // a null path. The polarity is ModuleIsOurOwn's: a seam that fails must never
    // widen what is allowed.
    //
    // OFFLINE BY FLAG, measured rather than assumed: WTD_REVOKE_NONE and
    // WTD_CACHE_ONLY_URL_RETRIEVAL, under which the probe's verdicts did not
    // change with every adapter disabled. cryptnet.dll still maps; no request is
    // made. NFR-10 and CLAUDE.md rule 8 are what that measurement protects.
    //
    // Reached only on a fragment hit, which on a measured machine is approximately
    // never — ~2-4 ms per verified module, inside a 30 s loop, on a scan set that
    // three real titles left empty.
    Collected (*ModuleSignerOrganisation)(const wchar_t* modulePath, char* out, std::size_t cap) = nullptr;

    // §S22 — is the DLL we were asked to inject one of OUR OWN binaries?
    //
    // The guard gated the target and nothing gated the payload. Every check
    // above answers "is it safe to be inside this process"; none of them asks
    // "and what are we putting there". `dllPath` arrives from the caller across
    // the C ABI, so without this the shipped FrameLedger.Guard.dll exports a
    // documented "load an arbitrary DLL into an arbitrary x64 process" primitive
    // — which is §S9's user-runnable injector, the exact thing this project
    // refused to ship, wearing a different shape.
    //
    // "Ours" means: the payload resolves — through symlinks, 8.3 names and
    // junctions — into the SAME DIRECTORY the guard's own code was loaded from,
    // compared by file id, exactly as §S18 compares directories. Equality, not
    // containment, for the same reason §S18 chose it.
    //
    // kFailed and a null seam both mean REFUSE. This is the ordinary polarity of
    // every other seam here, not the inverted one ProcessIsOurOwn needs: there,
    // "could not tell" must not SUPPRESS a refusal; here, "could not tell" must
    // not AUTHORISE a load.
    //
    // What it does NOT cover, stated here rather than discovered later:
    //   - It is not proof the payload is FrameLedger.Overlay.dll. It is proof of
    //     WHERE the bytes live. Anyone who can write to that directory can
    //     already replace FrameLedger.Guard.dll itself, which
    //     NativeAntiCheatGuard calls a worse outcome than any other DLL-hijack in
    //     this application — so the check is exactly as strong as the install
    //     location and no stronger. The project ships unsigned (CLAUDE.md rule
    //     9), so there is no integrity check on that directory's contents.
    //   - It is not atomic with the load. The remote LoadLibraryW re-resolves the
    //     path, so a file swapped between the check and the call is not caught.
    //     Same trust base as above: that swap needs write access we have already
    //     conceded.
    Collected (*PayloadIsOurOwn)(const wchar_t* dllPath, bool* isOurs) = nullptr;
};

// TWO SEAMS, ONE IMPLEMENTATION. SystemSources() wires both ModuleIsOurOwn and
// PayloadIsOurOwn to the same function: they ask the identical question — does
// this file resolve into the directory the guard's own code was loaded from —
// and two implementations of one question is the "second matcher that can
// disagree" this whole layer is arranged to avoid (§S15 item 1).
//
// They are separate POINTERS only so 14_TESTING's matrix can force each failure
// independently: a payload that cannot be identified and a module that cannot be
// located are different scenarios reaching different code, and a single seam
// would make one of them untestable without disturbing the other.

// The real Windows implementations. Behaviour measured in spike-notes.md §1;
// notably EnumerateModules reports kFailed on ERROR_PARTIAL_COPY (a suspended
// target) and uses LIST_MODULES_ALL, without which a 32-bit target under-reports
// by more than half AS A SUCCESS.
[[nodiscard]] Sources SystemSources() noexcept;

// ---------------------------------------------------------------------------
// The chokepoint
// ---------------------------------------------------------------------------

// Collect evidence, match, and — only if every check passes — inject.
//
// THERE IS NO Check() THAT RETURNS A VERDICT FOR SOMEBODY ELSE TO ACT ON. That
// shape was considered and rejected (§S13(b)): a clearance that escapes can be
// ignored by a caller who never asks for one, whereas an injection primitive
// with no external symbol cannot be reached at all. It lives in an anonymous
// namespace inside fl_guard.cpp, and tools/chokepoint-check.ps1 fails the build
// if any other translation unit names it.
//
// THE EVIDENCE IS NOT A PARAMETER EITHER. These take no Sources: they always
// use SystemSources(). The seam that the fail-closed matrix needs is real, but
// it is compiled out of every shipping target — see FL_GUARD_TESTABLE below.
// While the injection primitive was a stub, a caller passing all-clean fakes
// was a theoretical hole; the moment injection became real it would have been
// a way into a game process that never consulted a single genuine signal.
[[nodiscard]] Verdict GuardedInject(std::uint32_t targetPid, const wchar_t* dllPath) noexcept;

// LAUNCH MODE. Poll `targetPid` -- every 50 ms, up to `timeoutMs` -- until it has
// mapped a presentation runtime (dxgi.dll with d3d11.dll or d3d12.dll, or
// opengl32.dll, or vulkan-1.dll: the earliest externally observable moment at which
// a swapchain can exist), then run GuardedInject in full. The poll reads module
// names through the same seam the module scan uses and matches NOTHING against the
// blocklist: it decides WHEN the guard runs, never WHETHER it passes. A target that
// exits first, or never maps a runtime inside the budget, is refused with
// kLaunchTargetExited / kLaunchNoPresentationRuntime and nothing is injected.
// 04_CAPTURE §Launch mode; 20_OPEN_QUESTIONS §S1 / §S13(c).
[[nodiscard]] Verdict GuardedInjectWhenReady(std::uint32_t targetPid, const wchar_t* dllPath,
                                             std::uint32_t timeoutMs) noexcept;

// Evaluate the guard WITHOUT injecting. Exists for the 30 s in-session re-scan
// (19_SAFETY §During a session), which must reach a verdict on a process it is
// already inside and has nothing to inject. Deliberately cannot be used to
// pre-authorise an injection: it takes no dll path and returns no token, so the
// only way to act on a pass is to call GuardedInject, which re-collects.
[[nodiscard]] Verdict Evaluate(std::uint32_t targetPid) noexcept;

#ifdef FL_GUARD_TESTABLE
// ---------------------------------------------------------------------------
// TEST-ONLY. Defined by exactly one target — src/native/tests — and by nothing
// that ships. tools/chokepoint-check.ps1 fails the build if any other
// CMakeLists defines it.
//
// 14_TESTING's matrix requires forcing EnumProcessModulesEx failures, partial
// module lists, unreadable processes and a denied service query. None of that
// is reachable without injectable evidence, and an input whose failure path
// cannot be exercised is an input whose failure path is unverified. So the seam
// exists — and is unavailable to anything a user runs.
// ---------------------------------------------------------------------------
[[nodiscard]] Verdict EvaluateWithSources(std::uint32_t targetPid, const Sources& sources) noexcept;
[[nodiscard]] Verdict GuardedInjectWithSources(std::uint32_t targetPid, const wchar_t* dllPath,
                                               const Sources& sources) noexcept;
[[nodiscard]] Verdict GuardedInjectWhenReadyWithSources(std::uint32_t targetPid, const wchar_t* dllPath,
                                                        std::uint32_t timeoutMs, const Sources& sources) noexcept;
#endif

// Human-readable reason, for logs and for mapping to resx keys.
[[nodiscard]] const char* ReasonName(Reason r) noexcept;

}    // namespace fl::guard

#endif    // FL_GUARD_H
