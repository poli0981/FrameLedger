namespace FrameLedger.Application.Persistence;

public interface IHardwareSnapshotRepository
{
    /// <summary>The id of the row with this snapshot's hash, inserting it (stamped <paramref name="capturedAt"/>) when absent.</summary>
    ValueTask<long> EnsureAsync(HardwareSnapshot snapshot, DateTimeOffset capturedAt, CancellationToken ct = default);

    ValueTask<HardwareSnapshot?> FindAsync(long id, CancellationToken ct = default);
}
