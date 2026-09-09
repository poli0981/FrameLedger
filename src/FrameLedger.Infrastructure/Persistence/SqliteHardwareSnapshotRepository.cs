using System.Data.Common;
using Dapper;
using FrameLedger.Application.Persistence;

namespace FrameLedger.Infrastructure.Persistence;

/// <summary><c>hardware_snapshots</c>, deduplicated by hash.</summary>
public sealed class SqliteHardwareSnapshotRepository : IHardwareSnapshotRepository
{
    private const string _insert =
        "INSERT INTO hardware_snapshots (hash, cpu_name, gpu_name, gpu_driver, ram_gb, os_build, display_res, display_hz, captured_at) "
        + "VALUES (@hash, @cpu, @gpu, @driver, @ram, @os, @res, @hz, @at) RETURNING id";

    private const string _select =
        "SELECT cpu_name, gpu_name, gpu_driver, ram_gb, os_build, display_res, display_hz FROM hardware_snapshots WHERE id = @id";

    private readonly LedgerDatabase _db;

    public SqliteHardwareSnapshotRepository(LedgerDatabase db) => _db = db ?? throw new ArgumentNullException(nameof(db));

    public ValueTask<long> EnsureAsync(HardwareSnapshot snapshot, DateTimeOffset capturedAt, CancellationToken ct = default)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        return _db.WriteAsync(async (c, tx, token) =>
        {
            string hash = snapshot.Hash;
            long? existing = await c.ExecuteScalarAsync<long?>(new CommandDefinition(
                "SELECT id FROM hardware_snapshots WHERE hash = @hash", new { hash }, tx, cancellationToken: token)).ConfigureAwait(false);
            if (existing is long id)
            {
                return id;
            }

            var p = new
            {
                hash,
                cpu = snapshot.CpuName,
                gpu = snapshot.GpuName,
                driver = snapshot.GpuDriver,
                ram = snapshot.RamGb,
                os = snapshot.OsBuild,
                res = snapshot.DisplayRes,
                hz = snapshot.DisplayHz,
                at = capturedAt.ToUnixTimeMilliseconds(),
            };
            return await c.ExecuteScalarAsync<long>(new CommandDefinition(_insert, p, tx, cancellationToken: token)).ConfigureAwait(false);
        }, ct);
    }

    public ValueTask<HardwareSnapshot?> FindAsync(long id, CancellationToken ct = default) =>
        _db.ReadAsync((c, token) => SqliteReaders.ReadOneAsync(c, new CommandDefinition(_select, new { id }, cancellationToken: token), Read), ct);

    private static HardwareSnapshot Read(DbDataReader r) => new()
    {
        CpuName = SqliteReaders.String(r, 0),
        GpuName = SqliteReaders.String(r, 1),
        GpuDriver = SqliteReaders.String(r, 2),
        RamGb = SqliteReaders.Double(r, 3),
        OsBuild = SqliteReaders.String(r, 4),
        DisplayRes = SqliteReaders.String(r, 5),
        DisplayHz = SqliteReaders.Double(r, 6),
    };
}
