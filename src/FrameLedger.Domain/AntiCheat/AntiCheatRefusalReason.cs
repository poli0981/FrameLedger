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
}
