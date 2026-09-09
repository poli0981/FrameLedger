using System.Data.Common;
using Dapper;
using FrameLedger.Application.Consent;
using FrameLedger.Domain.AntiCheat;
using FrameLedger.Domain.Consent;
using Microsoft.Data.Sqlite;

namespace FrameLedger.Infrastructure.Persistence;

/// <summary>
/// The <c>games</c> table's consent columns behind <see cref="IGameConsentStore"/> — the adapter
/// <c>04_CAPTURE</c> §The guard said P2 would write.
/// </summary>
/// <remarks>
/// <para>
/// <b>It ships, and that changes what keeps CLAUDE.md rule 1 true.</b> The file-backed store it replaces
/// lived in the unshipped capture host so that no published binary could mint consent. This one is in
/// <c>FrameLedger.Infrastructure</c>, inside both publish closures, and <c>GameConsentRecord.Stored</c>'s
/// <c>InternalsVisibleTo</c> list now names this assembly. What holds rule 1 is therefore no longer
/// packaging: it is that the only producers of an acknowledgement are a disclosure shown to a human
/// (the capture host's verb today; the Agent's console verb in PR-F, decision D4; the UI's dialog in P3),
/// that the provenance is recorded by NAME, and that every anti-cheat check still runs afterwards.
/// <c>20_OPEN_QUESTIONS</c> §S27 carries the restatement.
/// </para>
/// <para>
/// The semantics are the file store's, unchanged: a grant merges the block state forward and cannot clear
/// it; a block forces the toggle off and preserves the stamp; a revoke withdraws the stamp; a default
/// verdict records "could not verify" (<c>hook_prescan_state = 'unverified'</c>) and never a block; a
/// re-grant against a different binary cannot inherit an existing block. Every failure returns
/// <see cref="ConsentWriteOutcome.Failed"/> rather than throwing past the port.
/// </para>
/// </remarks>
public sealed class SqliteGameConsentStore : IGameConsentStore
{
    private const string _columns =
        "exe_path, exe_size_bytes, exe_mtime_ms, hook_enabled, hook_consent_at, hook_consent_provenance, "
        + "hook_consent_disclosure_version, hook_blocked_reason, hook_prescan_state, updated_at";

    private const string _selectOne = $"SELECT {_columns} FROM games WHERE exe_path = @path";

    private const string _selectEnabled = $"SELECT {_columns} FROM games WHERE hook_enabled = 1 ORDER BY exe_path";

    private const string _insertGrant =
        "INSERT INTO games (name, exe_path, exe_size_bytes, exe_mtime_ms, hook_enabled, hook_consent_at, "
        + "hook_consent_provenance, hook_consent_disclosure_version, added_at, updated_at) "
        + "VALUES (@name, @path, @size, @mtime, 1, @at, @provenance, @disclosure, @at, @at)";

    // The block columns are NOT in the SET list: that is the merge-forward, expressed as an omission.
    private const string _updateGrant =
        "UPDATE games SET exe_size_bytes = @size, exe_mtime_ms = @mtime, hook_enabled = 1, hook_consent_at = @at, "
        + "hook_consent_provenance = @provenance, hook_consent_disclosure_version = @disclosure, updated_at = @at "
        + "WHERE exe_path = @path";

    private const string _revoke =
        "UPDATE games SET hook_enabled = 0, hook_consent_at = NULL, hook_consent_provenance = @provenance, "
        + "hook_consent_disclosure_version = '', updated_at = @at WHERE exe_path = @path";

    private const string _insertBlock =
        "INSERT INTO games (name, exe_path, exe_size_bytes, exe_mtime_ms, hook_enabled, hook_blocked_reason, "
        + "hook_prescan_state, added_at, updated_at) "
        + "VALUES (@name, @path, @size, @mtime, @enabled, @reason, @state, @at, @at)";

    // The consent columns are NOT in the SET list: the stamp and its provenance are PRESERVED across a block.
    private const string _updateBlock =
        "UPDATE games SET exe_size_bytes = @size, exe_mtime_ms = @mtime, hook_enabled = @enabled, "
        + "hook_blocked_reason = @reason, hook_prescan_state = @state, updated_at = @at WHERE exe_path = @path";

    private readonly LedgerDatabase _db;

    public SqliteGameConsentStore(LedgerDatabase db) => _db = db ?? throw new ArgumentNullException(nameof(db));

    /// <inheritdoc />
    public async ValueTask<GameConsentRecord> FindAsync(string normalisedExePath, CancellationToken ct = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(normalisedExePath);

        try
        {
            Row? row = await _db.ReadAsync((c, token) => ReadRowAsync(c, null, normalisedExePath, token), ct).ConfigureAwait(false);
            return row?.ToRecord() ?? default;
        }
        catch (SqliteException)
        {
            // An unreadable store consents to nothing. Same answer as "no record", and for the same reason.
            return default;
        }
    }

    /// <inheritdoc />
    public async ValueTask<IReadOnlyList<GameConsentRecord>> ListEnabledAsync(CancellationToken ct = default)
    {
        try
        {
            IReadOnlyList<Row> rows = await _db.ReadAsync(
                (c, token) => SqliteReaders.ReadAllAsync(c, new CommandDefinition(_selectEnabled, cancellationToken: token), Row.From),
                ct).ConfigureAwait(false);
            return [.. rows.Select(r => r.ToRecord())];
        }
        catch (SqliteException)
        {
            return [];
        }
    }

    /// <inheritdoc />
    public async ValueTask<ConsentWriteOutcome> RecordOperatorAcknowledgementAsync(
        OperatorAcknowledgement acknowledgement, CancellationToken ct = default)
    {
        ArgumentNullException.ThrowIfNull(acknowledgement);

        try
        {
            return await _db.WriteAsync(async (c, tx, token) =>
            {
                ExecutableFingerprint fp = acknowledgement.Fingerprint;
                Row? existing = await ReadRowAsync(c, tx, fp.ExePath, token).ConfigureAwait(false);

                // A FINGERPRINT THAT DOES NOT MATCH THE STORED ONE IS REFUSED when a BLOCK is at stake — never
                // silently re-pointed at a different binary. An ordinary re-grant after a patch keeps working.
                if (existing is not null && existing.BlockedReason is not null && !existing.Fingerprint.Matches(fp))
                {
                    return ConsentWriteOutcome.StaleFingerprint;
                }

                // A GRANT MAY NOT CLEAR A BLOCK: the acknowledgement carries neither field, so the row's own
                // values are the only source and there is nothing for a caller to override.
                long at = acknowledgement.AcknowledgedAt.ToUnixTimeMilliseconds();
                var p = new
                {
                    path = fp.ExePath,
                    name = System.IO.Path.GetFileNameWithoutExtension(fp.ExePath),
                    size = fp.SizeBytes,
                    mtime = fp.MtimeUnixMs,
                    at,
                    provenance = nameof(ConsentProvenance.UnshippedHostOperator),
                    disclosure = acknowledgement.DisclosureVersion,
                };
                await c.ExecuteAsync(new CommandDefinition(existing is null ? _insertGrant : _updateGrant, p, tx, cancellationToken: token))
                    .ConfigureAwait(false);
                return ConsentWriteOutcome.Written;
            }, ct).ConfigureAwait(false);
        }
        catch (SqliteException)
        {
            return ConsentWriteOutcome.Failed;
        }
    }

    /// <inheritdoc />
    public async ValueTask<ConsentWriteOutcome> RevokeAsync(string normalisedExePath, CancellationToken ct = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(normalisedExePath);

        try
        {
            return await _db.WriteAsync(async (c, tx, token) =>
            {
                // The stamp goes with the toggle here, unlike a block: a revoke IS the withdrawal, so being
                // shown the disclosure again is the correct consequence. hook_blocked_reason is untouched.
                var p = new
                {
                    path = normalisedExePath,
                    provenance = nameof(ConsentProvenance.NotRecorded),
                    at = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                };
                int rows = await c.ExecuteAsync(new CommandDefinition(_revoke, p, tx, cancellationToken: token)).ConfigureAwait(false);
                return rows == 0 ? ConsentWriteOutcome.NotFound : ConsentWriteOutcome.Written;
            }, ct).ConfigureAwait(false);
        }
        catch (SqliteException)
        {
            return ConsentWriteOutcome.Failed;
        }
    }

    /// <inheritdoc />
    public async ValueTask<ConsentWriteOutcome> RecordGuardBlockAsync(
        ExecutableFingerprint fingerprint, AntiCheatVerdict refusal, CancellationToken ct = default)
    {
        // NOTHING MANAGED AUTHORS AN ANTI-CHEAT FACT: an allowing verdict cannot become a block.
        if (refusal.IsAllowed)
        {
            return ConsentWriteOutcome.Failed;
        }

        // A DEFAULT-CONSTRUCTED VERDICT HAS SCANNED NOTHING: a refusal nobody produced is "could not
        // verify", not a block. 05_DETECTION forbids both collapses.
        bool evaluated = refusal.Reason != AntiCheatRefusalReason.Allow && !string.IsNullOrEmpty(refusal.Family + refusal.Signal);
        bool unverified = !evaluated || refusal.Reason == AntiCheatRefusalReason.PreScanFailed;

        try
        {
            return await _db.WriteAsync(async (c, tx, token) =>
            {
                Row? existing = await ReadRowAsync(c, tx, fingerprint.ExePath, token).ConfigureAwait(false);
                var p = new
                {
                    path = fingerprint.ExePath,
                    name = System.IO.Path.GetFileNameWithoutExtension(fingerprint.ExePath),
                    size = fingerprint.SizeBytes,
                    mtime = fingerprint.MtimeUnixMs,
                    // Forced to 0 on a real block (19_SAFETY); an unverified pre-scan does NOT disable the toggle.
                    enabled = unverified && existing?.HookEnabled == true ? 1 : 0,
                    reason = unverified ? existing?.BlockedReason : $"{refusal.Reason}: {refusal.Family} {refusal.Signal}".Trim(),
                    state = unverified ? "unverified" : "blocked",
                    at = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                };
                await c.ExecuteAsync(new CommandDefinition(existing is null ? _insertBlock : _updateBlock, p, tx, cancellationToken: token))
                    .ConfigureAwait(false);
                return ConsentWriteOutcome.Written;
            }, ct).ConfigureAwait(false);
        }
        catch (SqliteException)
        {
            return ConsentWriteOutcome.Failed;
        }
    }

    private static Task<Row?> ReadRowAsync(SqliteConnection c, SqliteTransaction? tx, string path, CancellationToken ct) =>
        SqliteReaders.ReadOneAsync(c, new CommandDefinition(_selectOne, new { path }, tx, cancellationToken: ct), Row.From);

    /// <summary>The consent columns of one row, read by ordinal in <see cref="_columns"/>' order.</summary>
    private sealed record Row(
        ExecutableFingerprint Fingerprint,
        bool HookEnabled,
        long? ConsentedAtMs,
        string ProvenanceName,
        string DisclosureVersion,
        string? BlockedReason,
        string PreScanState,
        long UpdatedAtMs)
    {
        public static Row From(DbDataReader r) => new(
            new ExecutableFingerprint
            {
                ExePath = r.GetString(0),
                SizeBytes = SqliteReaders.Int64(r, 1) ?? 0,
                MtimeUnixMs = SqliteReaders.Int64(r, 2) ?? 0,
            },
            r.GetInt64(3) != 0,
            SqliteReaders.Int64(r, 4),
            r.GetString(5),
            r.GetString(6),
            SqliteReaders.String(r, 7),
            r.GetString(8),
            r.GetInt64(9));

        public GameConsentRecord ToRecord() => GameConsentRecord.Stored(
            Fingerprint,
            HookEnabled,
            ConsentedAtMs is { } ms ? DateTimeOffset.FromUnixTimeMilliseconds(ms) : null,
            ParseProvenance(ProvenanceName),
            DisclosureVersion,
            BlockedReason,
            string.Equals(PreScanState, "unverified", StringComparison.Ordinal),
            DateTimeOffset.FromUnixTimeMilliseconds(UpdatedAtMs));
    }

    /// <summary>
    /// Only <see cref="ConsentProvenance"/>'s declared NAMES are accepted, exactly cased. <c>Enum.TryParse</c>
    /// alone parses numeric strings too and does not check the result against the declared members; this is
    /// the field that decides whether a timestamp counts as consent.
    /// </summary>
    private static ConsentProvenance ParseProvenance(string name) =>
        Enum.TryParse(name, ignoreCase: false, out ConsentProvenance p)
        && Enum.IsDefined(p)
        && string.Equals(p.ToString(), name, StringComparison.Ordinal)
            ? p
            : ConsentProvenance.NotRecorded;
}
