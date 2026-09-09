using Dapper;
using FrameLedger.Application.Persistence;

namespace FrameLedger.Infrastructure.Persistence;

/// <summary>READ-ONLY <c>legal_acceptance</c>. There is deliberately no write here (FR-11 is the UI's, P3).</summary>
public sealed class SqliteLegalAcceptanceStore : ILegalAcceptanceStore
{
    private readonly LedgerDatabase _db;

    public SqliteLegalAcceptanceStore(LedgerDatabase db) => _db = db ?? throw new ArgumentNullException(nameof(db));

    public ValueTask<LegalAcceptance?> FindAsync(string document, CancellationToken ct = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(document);
        return _db.ReadAsync((c, token) => SqliteReaders.ReadOneAsync(
            c,
            new CommandDefinition("SELECT version, accepted_at FROM legal_acceptance WHERE doc = @document", new { document }, cancellationToken: token),
            r => new LegalAcceptance(document, r.GetString(0), DateTimeOffset.FromUnixTimeMilliseconds(r.GetInt64(1)))), ct);
    }
}
