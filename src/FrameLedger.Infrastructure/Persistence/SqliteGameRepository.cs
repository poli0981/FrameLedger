using System.Data.Common;
using Dapper;
using FrameLedger.Application.Persistence;
using FrameLedger.Domain.Consent;
using Microsoft.Data.Sqlite;

namespace FrameLedger.Infrastructure.Persistence;

/// <summary>
/// The <c>games</c> table minus its consent columns. Nothing here can set <c>hook_enabled</c> to 1, stamp
/// <c>hook_consent_at</c>, or touch <c>hook_blocked_reason</c>: those are <see cref="SqliteGameConsentStore"/>'s,
/// and a second writer of the same columns would be the "two views of one state" the port forbids.
/// </summary>
public sealed class SqliteGameRepository : IGameRepository
{
    private const string _columns =
        "id, name, exe_path, exe_size_bytes, exe_mtime_ms, hook_enabled, hook_blocked_reason, hook_autodisabled_reason, "
        + "hook_crash_count, hook_last_injected_at, added_at, updated_at";

    private const string _selectByPath = $"SELECT {_columns} FROM games WHERE exe_path = @path";

    private const string _selectAll = $"SELECT {_columns} FROM games ORDER BY name, exe_path";

    private const string _insert =
        "INSERT INTO games (name, exe_path, exe_size_bytes, exe_mtime_ms, added_at, updated_at) "
        + "VALUES (@name, @path, @size, @mtime, @now, @now)";

    private const string _autoDisable =
        "UPDATE games SET hook_enabled = 0, hook_autodisabled_reason = @reason, hook_autodisabled_at = @at, updated_at = @at WHERE id = @id";

    private const string _crash =
        "UPDATE games SET hook_crash_count = hook_crash_count + 1, updated_at = @now WHERE id = @id RETURNING hook_crash_count";

    private const string _injected = "UPDATE games SET hook_last_injected_at = @at, updated_at = @at WHERE id = @id";

    private readonly LedgerDatabase _db;

    public SqliteGameRepository(LedgerDatabase db) => _db = db ?? throw new ArgumentNullException(nameof(db));

    public ValueTask<GameRow> EnsureAsync(ExecutableFingerprint fingerprint, string name, CancellationToken ct = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        return _db.WriteAsync(async (c, tx, token) =>
        {
            GameRow? existing = await ReadByPathAsync(c, tx, fingerprint.ExePath, token).ConfigureAwait(false);
            if (existing is not null)
            {
                return existing;
            }

            // Hooking OFF for every newly added game (19_SAFETY, CLAUDE.md rule 1): the row exists so a
            // Tier-2 session has somewhere to land; nothing about it says the user enabled anything.
            long now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            var p = new { name, path = fingerprint.ExePath, size = fingerprint.SizeBytes, mtime = fingerprint.MtimeUnixMs, now };
            await c.ExecuteAsync(new CommandDefinition(_insert, p, tx, cancellationToken: token)).ConfigureAwait(false);
            return (await ReadByPathAsync(c, tx, fingerprint.ExePath, token).ConfigureAwait(false))!;
        }, ct);
    }

    public ValueTask<GameRow?> FindAsync(string normalisedExePath, CancellationToken ct = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(normalisedExePath);
        return _db.ReadAsync((c, token) => ReadByPathAsync(c, null, normalisedExePath, token), ct);
    }

    public ValueTask<IReadOnlyList<GameRow>> ListAsync(CancellationToken ct = default) =>
        _db.ReadAsync((c, token) => SqliteReaders.ReadAllAsync(c, new CommandDefinition(_selectAll, cancellationToken: token), Read), ct);

    public ValueTask<bool> AutoDisableHookAsync(long gameId, string reason, DateTimeOffset at, CancellationToken ct = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(reason);
        return _db.WriteAsync(async (c, tx, token) => await c.ExecuteAsync(new CommandDefinition(
            _autoDisable, new { id = gameId, reason, at = at.ToUnixTimeMilliseconds() }, tx, cancellationToken: token)).ConfigureAwait(false) == 1, ct);
    }

    public ValueTask<int> RecordCrashAsync(long gameId, CancellationToken ct = default) =>
        _db.WriteAsync((c, tx, token) => c.ExecuteScalarAsync<int>(new CommandDefinition(
            _crash, new { id = gameId, now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() }, tx, cancellationToken: token)), ct);

    public ValueTask<bool> RecordInjectionAsync(long gameId, DateTimeOffset at, CancellationToken ct = default) =>
        _db.WriteAsync(async (c, tx, token) => await c.ExecuteAsync(new CommandDefinition(
            _injected, new { id = gameId, at = at.ToUnixTimeMilliseconds() }, tx, cancellationToken: token)).ConfigureAwait(false) == 1, ct);

    private static Task<GameRow?> ReadByPathAsync(SqliteConnection c, SqliteTransaction? tx, string path, CancellationToken ct) =>
        SqliteReaders.ReadOneAsync(c, new CommandDefinition(_selectByPath, new { path }, tx, cancellationToken: ct), Read);

    private static GameRow Read(DbDataReader r) => new()
    {
        Id = r.GetInt64(0),
        Name = r.GetString(1),
        Fingerprint = new ExecutableFingerprint
        {
            ExePath = r.GetString(2),
            SizeBytes = SqliteReaders.Int64(r, 3) ?? 0,
            MtimeUnixMs = SqliteReaders.Int64(r, 4) ?? 0,
        },
        HookEnabled = r.GetInt64(5) != 0,
        HookBlockedReason = SqliteReaders.String(r, 6),
        HookAutoDisabledReason = SqliteReaders.String(r, 7),
        HookCrashCount = (int)r.GetInt64(8),
        HookLastInjectedAt = SqliteReaders.Int64(r, 9) is { } injected ? DateTimeOffset.FromUnixTimeMilliseconds(injected) : null,
        AddedAt = DateTimeOffset.FromUnixTimeMilliseconds(r.GetInt64(10)),
        UpdatedAt = DateTimeOffset.FromUnixTimeMilliseconds(r.GetInt64(11)),
    };
}
