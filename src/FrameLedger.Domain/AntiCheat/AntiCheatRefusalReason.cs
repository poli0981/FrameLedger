namespace FrameLedger.Domain.AntiCheat;

/// <summary>
/// Why the anti-cheat guard refused, mirrored from the native
/// <c>fl::guard::Reason</c>.
/// </summary>
/// <remarks>
/// <para>
/// This is a MIRROR, not a second source of truth. The guard itself lives in
/// C++ (<c>20_OPEN_QUESTIONS</c> §S13(a)); nothing managed matches a blocklist,
/// because two matchers that can disagree is a fail-open by construction — the
/// day they diverge one of them is wrong and nothing tells you which.
/// </para>
/// <para>
/// The values are asserted against the native side by a test that reads each
/// name back through <c>FlGuardReasonName</c>, so a value added on one side and
/// forgotten on the other fails the build rather than quietly showing the user
/// the wrong refusal.
/// </para>
/// </remarks>
public enum AntiCheatRefusalReason
{
    /// <summary>Every check passed. The only value that permits injection.</summary>
    Allow = 0,

    // Check 1 — the target module scan, across the §S16 scan set.
    BlockedModule = 1,

    /// <summary>Enumeration returned a partial answer. Not "nothing found".</summary>
    ModuleScanFailed = 2,

    /// <summary>The process could not be opened. "Could not look" is not "clean".</summary>
    ProcessUnreadable = 3,

    /// <summary>The §S16 scan set could not be established.</summary>
    ProcessTreeUnavailable = 4,

    // Check 2 — the machine-wide driver scan.
    BlockedDriver = 5,
    DriverScanFailed = 6,

    // Check 2b — services, which are in no process tree.
    BlockedService = 7,

    /// <summary>Denied, rather than absent. Absent is a real answer; denied is not.</summary>
    ServiceQueryFailed = 8,

    // Check 3 — per-title lists. Two reasons this matches nothing today: both
    // arrays are empty in the shipped seed, and the matchers have no call site
    // — the check is UNWIRED, not merely unpopulated (20_OPEN_QUESTIONS §S14).
    BlockedExecutable = 9,
    BlockedStoreId = 10,

    // Check 4 — the static, pre-launch scan.
    AntiCheatDirectory = 11,
    AntiCheatFile = 12,

    /// <summary>Suspicious name fragment, not signed by a trusted vendor.</summary>
    SuspiciousUnsigned = 13,

    // The rules data itself is an input, and a broken one refuses.
    RulesUnreadable = 14,
    RulesMalformed = 15,

    /// <summary>Parsed, but a required family is missing. An empty blocklist is a fixture, never a ship state.</summary>
    RulesIncomplete = 16,

    /// <summary>
    /// Check 4 could not scan the game directory — absent, unlistable,
    /// truncated by a bound, or crossing a reparse point we will not follow.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Its own value rather than reusing <see cref="AntiCheatDirectory"/>:
    /// collapsing "we found anti-cheat" into "we could not look" is the exact
    /// defect that produced this project's worst finding.
    /// </para>
    /// <para>
    /// Appended rather than grouped with the other check-4 reasons. These
    /// values cross a C ABI, so inserting one would renumber every reason after
    /// it and silently change what a stored or logged value means.
    /// </para>
    /// </remarks>
    PreScanFailed = 17,

    /// <summary>
    /// Every check passed and the injection still did not happen.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>This is not an allow.</strong> The native side used to report a
    /// failed injection as <see cref="Allow"/> with the truth in a free-text
    /// signal, so <c>IsAllowed</c> was <c>true</c> for an injection that never
    /// occurred. Measured 2026-08-03 against a real title.
    /// </para>
    /// <para>
    /// Distinct from a refusal because the response differs: a refusal is
    /// permanent and names an anti-cheat family; this one may be transient and
    /// names none.
    /// </para>
    /// </remarks>
    InjectionFailed = 18,

    /// <summary>
    /// The target is a 32-bit process, so an x64 Overlay cannot load into it.
    /// </summary>
    /// <remarks>
    /// Permanent and expected rather than an error — the Overlay is x64-only
    /// (<c>20_OPEN_QUESTIONS</c> §Scope decisions), which is also why D3D9 is not
    /// a Tier-1 API. The UI should say so and offer Tier 2 rather than reporting
    /// a failure the user could act on.
    /// </remarks>
    TargetIsWow64 = 19,

    /// <summary>
    /// The DLL the caller asked us to inject is not one of FrameLedger's own
    /// binaries.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Every other value here is about the target process or the machine. This
    /// one is about the <em>payload</em>, and it exists because the gate had a
    /// hole of exactly that shape (<c>20_OPEN_QUESTIONS</c> §S22): the exported
    /// <c>FlGuardedInject</c> took a caller-supplied path and asked only whether
    /// a file was there, so the shipped guard was a documented "load any DLL
    /// into any x64 process without anti-cheat" primitive.
    /// </para>
    /// <para>
    /// Nothing was attempted when this is returned, which is why it is not a
    /// flavour of <see cref="InjectionFailed"/>. It means the payload does not
    /// resolve into the directory the guard itself was loaded from.
    /// </para>
    /// </remarks>
    PayloadNotOurs = 20,

    /// <summary>Hooking is off for this game — a game was added, not enabled.</summary>
    /// <remarks>
    /// <para>
    /// The three values below are refusals the <em>capture gate</em> makes and
    /// the native guard structurally cannot: consent and per-game enablement are
    /// records of something a human did, and they live in the Agent's database.
    /// They are mirrored anyway so there is one reason table and one place the
    /// UI maps to a string.
    /// </para>
    /// <para>
    /// All three used to return <see cref="BlockedExecutable"/> — check 3's
    /// code, which at the time the native guard could not produce at all (§S14:
    /// the matchers had no call site). The UI would have said "this title is on
    /// the per-title blocklist" when the truth was "you have not accepted the
    /// consent dialog".
    /// </para>
    /// <para>
    /// #52 wired check 3's executable half, so <see cref="BlockedExecutable"/>
    /// now has a native producer and means one specific thing. The separation
    /// above therefore matters more than when it was made, not less.
    /// </para>
    /// </remarks>
    HookNotEnabled = 21,

    /// <summary>The per-game consent dialog has not been accepted (FR-2.1).</summary>
    ConsentMissing = 22,

    /// <summary>
    /// The game was enabled once and has since become blocked — a patch added
    /// anti-cheat, or updated rules newly match (<c>19_SAFETY</c> §A game already
    /// enabled can become blocked later).
    /// </summary>
    /// <remarks>
    /// Honouring this in the capture gate is what stops a stale in-memory
    /// watchlist resurrecting the game after <c>hook_enabled</c> was forced to 0.
    /// </remarks>
    PreviouslyBlocked = 23,
}
