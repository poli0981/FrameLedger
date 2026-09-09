using FrameLedger.Infrastructure.Telemetry;

namespace FrameLedger.Infrastructure.Tests.Telemetry;

/// <summary>A scripted <see cref="IAdapterMemoryCounter"/>: which LUIDs have a counter, and what each read returns.</summary>
internal sealed class FakeMemoryCounter : IAdapterMemoryCounter
{
    private readonly Func<ulong, Func<ulong>?> _bind;
    private Func<ulong>? _read;

    /// <summary>Scripted per LUID.</summary>
    /// <param name="bind">Per LUID: a read function, or null when the counter does not exist there.</param>
    public FakeMemoryCounter(Func<ulong, Func<ulong>?> bind) => _bind = bind;

    public List<ulong> Opened { get; } = [];

    public int Reads { get; private set; }

    public bool Disposed { get; private set; }

    public bool TryOpen(ulong luid)
    {
        Opened.Add(luid);
        _read = _bind(luid);
        return _read is not null;
    }

    public ulong ReadDedicatedUsageBytes()
    {
        Reads++;
        return _read is null ? throw new InvalidOperationException("no counter is open") : _read();
    }

    public void Dispose() => Disposed = true;

    public static FakeMemoryCounter Constant(ulong bytes) => new(_ => () => bytes);

    public static FakeMemoryCounter Absent() => new(_ => null);
}
