using Dapper;
using FrameLedger.Application.Persistence;

namespace FrameLedger.Infrastructure.Persistence;

/// <summary>The <c>settings</c> table.</summary>
public sealed class SqliteSettingsStore : ISettingsStore
{
    private readonly LedgerDatabase _db;

    public SqliteSettingsStore(LedgerDatabase db) => _db = db ?? throw new ArgumentNullException(nameof(db));

    public ValueTask<string?> GetAsync(string key, CancellationToken ct = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(key);
        return _db.ReadAsync(
            (c, token) => c.ExecuteScalarAsync<string?>(new CommandDefinition(
                "SELECT value FROM settings WHERE key = @key", new { key }, cancellationToken: token)),
            ct);
    }

    public async ValueTask SetAsync(string key, string value, CancellationToken ct = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(key);
        ArgumentNullException.ThrowIfNull(value);
        await _db.WriteAsync(
            (c, tx, token) => c.ExecuteAsync(new CommandDefinition(
                "INSERT INTO settings (key, value) VALUES (@key, @value) ON CONFLICT(key) DO UPDATE SET value = excluded.value",
                new { key, value }, tx, cancellationToken: token)),
            ct).ConfigureAwait(false);
    }
}
