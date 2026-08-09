namespace FrameLedger.Domain.Consent;

/// <summary>
/// What a store knows about one game's hooking state: the three inputs
/// <c>HookedCaptureGate</c> needs, plus what it took to be entitled to them.
/// </summary>
/// <remarks>
/// <para>
/// These are <c>06_DATA_MODEL</c> §games' hook-state columns — <c>hook_enabled</c>,
/// <c>hook_consent_at</c>, <c>hook_blocked_reason</c> — and until now they existed
/// in that document and in no <c>.cs</c> file (§S27). P2's SQLite owns the rest of
/// the row; this is not a game library.
/// </para>
/// <para>
/// <b>Get-only properties and one factory, following
/// <c>AntiCheat.AntiCheatVerdict</c> rather than the <c>init</c> idiom the rest of
/// the tree uses.</b> The difference is load-bearing and was got wrong first: with
/// <c>init</c> accessors, a private constructor confines nothing, because a struct
/// always has an accessible parameterless constructor and an object initialiser
/// reaches every <c>init</c> member — so <c>default(GameConsentRecord) with
/// { HookEnabled = true, ConsentedAt = DateTimeOffset.UtcNow }</c> would mint a
/// consent record from nothing. That expression is §S27's rejected synthesis with
/// two extra words.
/// </para>
/// <para>
/// <b><see cref="Stored"/> is <c>internal</c>, and the reason is packaging.</b>
/// <c>FrameLedger.App</c> and <c>FrameLedger.Agent</c> both reference
/// <c>FrameLedger.Application</c>, which references this assembly, and
/// <c>12_BUILD</c> publishes both roots into one <c>out/app</c> — so a public
/// minting factory here would be a blessed, shipped API for producing consent
/// nobody gave, and the package-closure gate could not see it, because it walks
/// project references and Domain is legitimately in both closures. Only the
/// unshipped capture host and this assembly's tests can reach it. P2 adding an
/// adapter in <c>Infrastructure</c> means adding it to the
/// <c>InternalsVisibleTo</c> list, which is a deliberate, reviewable act rather
/// than a discovery.
/// </para>
/// <para>
/// <b>There is deliberately no <c>PermitsHooking</c> predicate.</b>
/// <c>HookedCaptureGate</c> is the only place that decides, because it is the only
/// place that produces the three distinct refusals — <c>HookNotEnabled</c>,
/// <c>ConsentMissing</c>, <c>PreviouslyBlocked</c> — that the UI has to tell apart.
/// A predicate here would be a second decider that collapses them.
/// </para>
/// </remarks>
public readonly record struct GameConsentRecord
{
    /// <summary>
    /// False on a default-constructed value. Same mechanism, and same reason, as
    /// <c>AntiCheatVerdict._evaluated</c>: a value nobody produced has recorded
    /// nothing and must not read as a record.
    /// </summary>
    private readonly bool _fromStore;

    private GameConsentRecord(
        ExecutableFingerprint fingerprint,
        bool hookEnabled,
        DateTimeOffset? consentedAt,
        ConsentProvenance provenance,
        string disclosureVersion,
        string? blockedReason,
        bool preScanUnverified,
        DateTimeOffset updatedAt)
    {
        Fingerprint = fingerprint;
        HookEnabled = hookEnabled;
        ConsentedAt = consentedAt;
        Provenance = provenance;
        DisclosureVersion = disclosureVersion;
        BlockedReason = blockedReason;
        PreScanUnverified = preScanUnverified;
        UpdatedAt = updatedAt;
        _fromStore = true;
    }

    /// <summary>Which executable this is about (<c>games.exe_path</c>, plus what detects a change).</summary>
    public ExecutableFingerprint Fingerprint { get; }

    /// <summary><c>games.hook_enabled</c>. Off for every newly added game (<c>19_SAFETY</c>).</summary>
    public bool HookEnabled { get; }

    /// <summary><c>games.hook_consent_at</c>. Null means the disclosure was never accepted.</summary>
    public DateTimeOffset? ConsentedAt { get; }

    /// <summary>What disclosure preceded <see cref="ConsentedAt"/>.</summary>
    /// <remarks>
    /// <c>games.hook_consent_at</c> is a bare timestamp, which cannot distinguish a
    /// stamp made after the FR-2.1 dialog from one made after anything else. This
    /// column has no counterpart in <c>06_DATA_MODEL</c> yet, and that document is
    /// updated in the same PR.
    /// </remarks>
    public ConsentProvenance Provenance { get; }

    /// <summary>
    /// Which wording the operator saw, so a later change to it does not silently
    /// re-interpret records already written.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Carried from the first record rather than added later, and the reason is that
    /// it cannot be added later. <c>legal_acceptance</c> stores
    /// <c>(doc, version, accepted_at)</c> while <c>games.hook_consent_at</c> stores a
    /// timestamp alone; retrofitting a version onto stamped records means treating
    /// unversioned consent as either current or stale, and both are wrong about some
    /// record. This PR writes the first record, so one string now forecloses nothing
    /// and its absence would foreclose everything.
    /// </para>
    /// <para>
    /// Empty when <see cref="Provenance"/> is
    /// <see cref="ConsentProvenance.NotRecorded"/>, because there was no wording.
    /// </para>
    /// </remarks>
    public string DisclosureVersion { get; }

    /// <summary>
    /// <c>games.hook_blocked_reason</c>. Non-null means a real guard verdict blocked
    /// this title; <c>19_SAFETY</c> forces <c>hook_enabled</c> to 0 and preserves
    /// <see cref="ConsentedAt"/>.
    /// </summary>
    public string? BlockedReason { get; }

    /// <summary>
    /// The static pre-scan could not reach an answer — neither a hit nor a pass.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Its own field rather than a value of <see cref="BlockedReason"/>, because one
    /// nullable string cannot carry three states and <c>05_DETECTION</c> forbids both
    /// collapses: folding it into "blocked" disables the toggle with no appeal on
    /// evidence nobody has, and clearing it is a fail-open.
    /// </para>
    /// <para>
    /// It is not routed through <c>HookedCaptureGate</c>. The gate has no reason code
    /// for it, and giving it one would force exactly the collapse above; a caller
    /// refuses on it before building a request.
    /// </para>
    /// </remarks>
    public bool PreScanUnverified { get; }

    /// <summary>When this record was last written, UTC.</summary>
    public DateTimeOffset UpdatedAt { get; }

    /// <summary>
    /// True only for a record an <c>IGameConsentStore</c> actually returned.
    /// </summary>
    /// <remarks>
    /// A default-constructed record reports false. Consumers must treat false as
    /// "no record exists", never as a record whose fields happen to be empty.
    /// </remarks>
    public bool IsFromStore => _fromStore;

    /// <summary>The only way to build a record that claims to have come from a store.</summary>
    /// <remarks>
    /// Internal on purpose — see the type remarks. Widening it to <c>public</c> puts a
    /// consent-minting API inside the shipped package.
    /// </remarks>
    internal static GameConsentRecord Stored(
        ExecutableFingerprint fingerprint,
        bool hookEnabled,
        DateTimeOffset? consentedAt,
        ConsentProvenance provenance,
        string? disclosureVersion,
        string? blockedReason,
        bool preScanUnverified,
        DateTimeOffset updatedAt) =>
        new(fingerprint, hookEnabled, consentedAt, provenance, disclosureVersion ?? string.Empty, blockedReason,
            preScanUnverified, updatedAt);
}
