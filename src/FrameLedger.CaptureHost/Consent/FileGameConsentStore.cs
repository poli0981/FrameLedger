using System.Text;
using System.Text.Json;
using FrameLedger.Application.Consent;
using FrameLedger.Domain.AntiCheat;
using FrameLedger.Domain.Consent;
using FrameLedger.Infrastructure.Io;

namespace FrameLedger.CaptureHost.Consent;

/// <summary>
/// The file behind <see cref="IGameConsentStore"/>, and the reason
/// <c>HookedCaptureGate</c>'s three inputs finally have a source that is not
/// synthesis (§S27).
/// </summary>
/// <remarks>
/// <para>
/// <b>It lives in the unshipped host, not in <c>FrameLedger.Infrastructure</c>.</b>
/// Both publish roots reference Infrastructure and <c>12_BUILD</c> puts them in one
/// <c>out/app</c>, so an adapter that mints consent records placed there would ship
/// inside the package with nothing but "no shipped code constructs it" keeping it
/// inert. Here it is outside the package by the same mechanism as the host, and one
/// gate covers both.
/// </para>
/// <para>
/// <b>The file lives beside the host binary, not under
/// <c>%LOCALAPPDATA%\FrameLedger</c>.</b> Three reasons, in order of weight: the
/// Agent is the ratified sole owner of that directory (§S18 blocker 3); a consent
/// record is a WIDENING input, unlike the rules file which can only narrow, so
/// confining it to a build tree that <c>git clean</c> removes bounds the blast
/// radius to the machine that built the host; and it is the same directory §S22
/// already keys the payload check on, which makes "this record belongs to this
/// unshipped build" structural rather than asserted.
/// </para>
/// </remarks>
internal sealed class FileGameConsentStore : IGameConsentStore
{
    private const int _fileVersion = 1;

    private readonly string _destination;

    /// <summary>The product location for this host: beside its own binary.</summary>
    public FileGameConsentStore()
        : this(Path.Combine(AppContext.BaseDirectory, "consent", "games.json"))
    {
    }

    /// <summary>Explicit path. Tests only.</summary>
    internal FileGameConsentStore(string destination) =>
        _destination = !string.IsNullOrWhiteSpace(destination)
            ? destination
            : throw new ArgumentException("a consent store needs a destination", nameof(destination));

    /// <summary>Where this store writes. Exposed so a test can assert its shape.</summary>
    public string Destination => _destination;

    /// <inheritdoc />
    public async ValueTask<GameConsentRecord> FindAsync(string normalisedExePath, CancellationToken ct = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(normalisedExePath);

        (ConsentFile file, bool readable) = await ReadAsync(ct).ConfigureAwait(false);
        if (!readable)
        {
            // An unreadable store consents to nothing. Same answer as "no record", and for the same
            // reason: both must refuse.
            return default;
        }

        ConsentFileEntry? entry = file.Games.FirstOrDefault(
            e => string.Equals(e.ExePath, normalisedExePath, StringComparison.OrdinalIgnoreCase));

        // THE FULL PATH, AND ONLY THE FULL PATH. 04_CAPTURE permits a filename fallback for the
        // WATCHLIST, where a wrong match costs a stale-path badge. Applied to consent the polarity
        // inverts: a different binary with the same filename would inherit an existing consent record.
        return entry is null ? default : ToRecord(entry);
    }

    /// <inheritdoc />
    public async ValueTask<IReadOnlyList<GameConsentRecord>> ListEnabledAsync(CancellationToken ct = default)
    {
        (ConsentFile file, bool readable) = await ReadAsync(ct).ConfigureAwait(false);
        return readable ? [.. file.Games.Where(e => e.HookEnabled).Select(ToRecord)] : [];
    }

    /// <inheritdoc />
    public async ValueTask<ConsentWriteOutcome> RecordOperatorAcknowledgementAsync(
        OperatorAcknowledgement acknowledgement, CancellationToken ct = default)
    {
        ArgumentNullException.ThrowIfNull(acknowledgement);

        (ConsentFile file, bool readable) = await ReadAsync(ct).ConfigureAwait(false);
        if (!readable)
        {
            // ABORT rather than merge against an empty store. The merge below is the only thing
            // carrying BlockedReason forward — so merging against nothing would silently CLEAR a
            // persisted guard block — and republishing would drop every other game's record.
            return ConsentWriteOutcome.Failed;
        }

        ConsentFileEntry? existing = file.Games.FirstOrDefault(
            e => string.Equals(e.ExePath, acknowledgement.Fingerprint.ExePath, StringComparison.OrdinalIgnoreCase));

        // A FINGERPRINT THAT DOES NOT MATCH THE STORED ONE IS REFUSED, not silently re-pointed at a
        // different binary. ConsentWriteOutcome.StaleFingerprint documented this and nothing produced
        // it — a declared-but-producerless value, the shape this PR invokes two files away to justify
        // ConsentProvenance having no FR-2.1 member.
        if (existing is not null
            && !new ExecutableFingerprint
            {
                ExePath = existing.ExePath,
                SizeBytes = existing.SizeBytes,
                MtimeUnixMs = existing.MtimeUnixMs,
            }.Matches(acknowledgement.Fingerprint)
            && existing.BlockedReason is not null)
        {
            // Only when a BLOCK is at stake. An ordinary re-grant after a patch is the normal way an
            // operator re-consents to an updated title and must keep working; what must not happen is
            // a re-grant against a different binary quietly inheriting the old one's block state.
            return ConsentWriteOutcome.StaleFingerprint;
        }

        // A GRANT MAY NOT CLEAR A BLOCK. The acknowledgement type carries neither field, so this merge
        // is the only source for them and there is nothing for a caller to override. 19_SAFETY forces
        // hook_enabled to 0 on a match and 06_DATA_MODEL defines a non-null reason as "toggle disabled";
        // a command that cleared it while the match still stood would be the "I understand, continue
        // anyway" button CLAUDE.md rule 2 forbids.
        var entry = new ConsentFileEntry
        {
            ExePath = acknowledgement.Fingerprint.ExePath,
            SizeBytes = acknowledgement.Fingerprint.SizeBytes,
            MtimeUnixMs = acknowledgement.Fingerprint.MtimeUnixMs,
            HookEnabled = true,
            ConsentedAtUnixMs = acknowledgement.AcknowledgedAt.ToUnixTimeMilliseconds(),
            Provenance = nameof(ConsentProvenance.UnshippedHostOperator),
            DisclosureVersion = acknowledgement.DisclosureVersion,
            BlockedReason = existing?.BlockedReason,
            PreScanUnverified = existing?.PreScanUnverified ?? false,
            UpdatedAtUnixMs = acknowledgement.AcknowledgedAt.ToUnixTimeMilliseconds(),
        };

        return await WriteAsync(file, entry, ct).ConfigureAwait(false);
    }

    /// <inheritdoc />
    public async ValueTask<ConsentWriteOutcome> RevokeAsync(string normalisedExePath, CancellationToken ct = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(normalisedExePath);

        (ConsentFile file, bool readable) = await ReadAsync(ct).ConfigureAwait(false);
        if (!readable)
        {
            return ConsentWriteOutcome.Failed;
        }

        ConsentFileEntry? existing = file.Games.FirstOrDefault(
            e => string.Equals(e.ExePath, normalisedExePath, StringComparison.OrdinalIgnoreCase));
        if (existing is null)
        {
            return ConsentWriteOutcome.NotFound;
        }

        // The stamp goes with the toggle here, unlike a block. A block preserves hook_consent_at because
        // the user did consent and a title changing under them is not a withdrawal; a revoke IS the
        // withdrawal, so being shown the disclosure again is the correct consequence.
        ConsentFileEntry revoked = existing with
        {
            HookEnabled = false,
            ConsentedAtUnixMs = null,
            Provenance = nameof(ConsentProvenance.NotRecorded),
            DisclosureVersion = string.Empty,
            UpdatedAtUnixMs = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
        };

        return await WriteAsync(file, revoked, ct).ConfigureAwait(false);
    }

    /// <inheritdoc />
    public async ValueTask<ConsentWriteOutcome> RecordGuardBlockAsync(
        ExecutableFingerprint fingerprint, AntiCheatVerdict refusal, CancellationToken ct = default)
    {
        // NOTHING MANAGED AUTHORS AN ANTI-CHEAT FACT. The method takes a verdict, and these two branches
        // are what stop one being manufactured from managed state (04_CAPTURE §The guard, §S15).
        if (refusal.IsAllowed)
        {
            return ConsentWriteOutcome.Failed;
        }

        (ConsentFile file, bool readable) = await ReadAsync(ct).ConfigureAwait(false);
        if (!readable)
        {
            return ConsentWriteOutcome.Failed;
        }

        ConsentFileEntry? existing = file.Games.FirstOrDefault(
            e => string.Equals(e.ExePath, fingerprint.ExePath, StringComparison.OrdinalIgnoreCase));

        // A DEFAULT-CONSTRUCTED VERDICT HAS SCANNED NOTHING. AntiCheatVerdict's default is a refusal
        // precisely so a forgotten assignment cannot read as permission — but a refusal nobody produced
        // is "could not verify", not a block. 05_DETECTION forbids both collapses: folding it into
        // blocked disables the toggle with no appeal, and clearing it is a fail-open.
        bool evaluated = refusal.Reason != AntiCheatRefusalReason.Allow
            && !string.IsNullOrEmpty(refusal.Family + refusal.Signal);
        bool unverified = !evaluated || refusal.Reason == AntiCheatRefusalReason.PreScanFailed;

        var entry = new ConsentFileEntry
        {
            ExePath = fingerprint.ExePath,
            SizeBytes = fingerprint.SizeBytes,
            MtimeUnixMs = fingerprint.MtimeUnixMs,

            // Forced to 0 on a real block (19_SAFETY). An unverified pre-scan does NOT disable the
            // toggle — that is the false refusal with no appeal 05_DETECTION names.
            HookEnabled = unverified && existing is not null && existing.HookEnabled,

            // PRESERVED. The user did consent; a block is not a withdrawal, and must not silently
            // require consenting again if the title is later cleared.
            ConsentedAtUnixMs = existing?.ConsentedAtUnixMs,
            Provenance = existing?.Provenance ?? nameof(ConsentProvenance.NotRecorded),
            DisclosureVersion = existing?.DisclosureVersion ?? string.Empty,

            BlockedReason = unverified
                ? existing?.BlockedReason
                : $"{refusal.Reason}: {refusal.Family} {refusal.Signal}".Trim(),
            PreScanUnverified = unverified || (existing?.PreScanUnverified ?? false),
            UpdatedAtUnixMs = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
        };

        return await WriteAsync(file, entry, ct).ConfigureAwait(false);
    }

    private static GameConsentRecord ToRecord(ConsentFileEntry e) =>
        GameConsentRecord.Stored(
            new ExecutableFingerprint { ExePath = e.ExePath, SizeBytes = e.SizeBytes, MtimeUnixMs = e.MtimeUnixMs },
            e.HookEnabled,
            e.ConsentedAtUnixMs is { } ms ? DateTimeOffset.FromUnixTimeMilliseconds(ms) : null,

            // AN UNRECOGNISED PROVENANCE READS AS NotRecorded, WHICH REFUSES — and this needed the
            // Enum.IsDefined that it did not have. The comment here used to claim that TryParse "over a
            // name accepts only what is declared". It does not: TryParse also parses NUMERIC strings and
            // does not validate the result against the declared members, so `"provenance": "1"` in the
            // file yielded UnshippedHostOperator and `"42"` yielded an undeclared value cast to the enum.
            // ConsentProvenance has exactly two members, and a test pins that count, precisely so an
            // unearned provenance cannot be expressed — a numeric string was an end-run around it.
            ParseProvenance(e.Provenance),
            e.DisclosureVersion,
            e.BlockedReason,
            e.PreScanUnverified,
            DateTimeOffset.FromUnixTimeMilliseconds(e.UpdatedAtUnixMs));

    /// <summary>
    /// Only <see cref="ConsentProvenance"/>'s declared NAMES are accepted.
    /// </summary>
    /// <remarks>
    /// <c>Enum.TryParse</c> alone is not enough: it parses numeric strings too and does
    /// not check the result against the declared members, so <c>"1"</c> and <c>"42"</c>
    /// both "succeed". This is the field that decides whether a timestamp counts as
    /// consent, and the enum's two-member count is a control a test pins.
    /// </remarks>
    private static ConsentProvenance ParseProvenance(string name) =>
        Enum.TryParse(name, ignoreCase: false, out ConsentProvenance p)
        && Enum.IsDefined(p)
        && string.Equals(p.ToString(), name, StringComparison.Ordinal)
            ? p
            : ConsentProvenance.NotRecorded;

    /// <summary>
    /// "There is no file" and "I could not read the file" are DIFFERENT, and conflating
    /// them let one failed read destroy the store.
    /// </summary>
    /// <remarks>
    /// Both used to produce an empty <see cref="ConsentFile"/>. Every write is a
    /// read-modify-write over that result, so an unreadable file made <c>existing</c>
    /// null — the merge that carries <c>BlockedReason</c> and <c>PreScanUnverified</c>
    /// forward carried nulls instead, **silently clearing a persisted guard block** —
    /// and the write then republished a file containing only the new entry, dropping
    /// every other game's record. A read that failed must abort the write, not seed it.
    /// </remarks>
    private async ValueTask<(ConsentFile File, bool Readable)> ReadAsync(CancellationToken ct)
    {
        var empty = new ConsentFile { Version = _fileVersion };
        if (!File.Exists(_destination))
        {
            return (empty, true);    // genuinely absent: an empty store is the right answer
        }

        try
        {
            byte[] bytes = await File.ReadAllBytesAsync(_destination, ct).ConfigureAwait(false);
            ConsentFile? file = JsonSerializer.Deserialize(bytes, ConsentFileJsonContext.Default.ConsentFile);

            // An unknown version reads as NO RECORDS and is NOT readable, so it refuses lookups and
            // refuses writes rather than being overwritten. Guessing at a shape we do not understand is
            // the one option that could turn an unreadable file into permission; silently replacing it
            // is the one that could destroy somebody's records.
            if (file is null || file.Version != _fileVersion)
            {
                return (empty, false);
            }

            return (file, true);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
        {
            return (empty, false);
        }
    }

    private async ValueTask<ConsentWriteOutcome> WriteAsync(ConsentFile file, ConsentFileEntry entry,
        CancellationToken ct)
    {
        List<ConsentFileEntry> games =
        [
            .. file.Games.Where(e => !string.Equals(e.ExePath, entry.ExePath, StringComparison.OrdinalIgnoreCase)),
            entry,
        ];

        byte[] bytes = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(
            new ConsentFile { Version = _fileVersion, Games = games },
            ConsentFileJsonContext.Default.ConsentFile));

        return await AtomicFileWrite.PublishAsync(_destination, bytes, ct).ConfigureAwait(false)
            ? ConsentWriteOutcome.Written
            : ConsentWriteOutcome.Failed;
    }
}
