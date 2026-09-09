using System.Diagnostics.CodeAnalysis;
using FrameLedger.Application.Telemetry;

namespace FrameLedger.Infrastructure.Telemetry;

/// <summary>
/// L1 — what the OS knows about the adapter, no licence, every vendor
/// (<c>docs/18_GPU_VENDOR_APIS.md</c> §L1): identity from DXGI, adapter-wide dedicated memory in
/// use from PDH.
/// </summary>
/// <remarks>
/// <para>
/// <b>What it carries:</b> the adapter's identity (name, LUID, ids, memory sizes, user-mode
/// driver version — <see cref="Adapters"/>, read once at <see cref="Start"/>) and one live
/// field, <see cref="GpuSample.VramAdapterMb"/>, from the PDH <c>GPU Adapter Memory</c> counter
/// bound to the selected adapter's LUID. Nothing else. <b>Utilisation is deliberately not here:</b>
/// <c>20_OPEN_QUESTIONS</c> §M10 measured that the PDH engine counters summed do not reproduce
/// the figure the doc invoked, so <c>LoadPct</c> is L2's vendor-reported load and is labelled
/// as such. Temperatures were never L1's (§L1: "that is what L2 is for").
/// </para>
/// <para>
/// <b>Which adapter:</b> the first hardware adapter in DXGI's high-performance order, until
/// <see cref="SelectAdapter"/> is given the LUID the Overlay published at the game's first
/// present — then that one, for the rest of the session. A software adapter is listed and
/// never selected by default. A machine whose counter set lacks the adapter still gets a sample
/// — name only, no fields — so the composite can carry DXGI's name for it.
/// </para>
/// <para>
/// <b>Reads on the caller's thread.</b> Unlike L2 there is no thread of its own: a PDH collect
/// is microseconds and cannot block on a driver the way LHM's tree walk can, so the poller
/// thread simply calls <see cref="TryRead"/>. Fault policy is the port's: a throw is a fault,
/// the second disables the layer for the session, and a machine with no adapter at all is an
/// answer (N/A, layer healthy), not a fault.
/// </para>
/// </remarks>
public sealed class BaselineTelemetrySource : IGpuTelemetrySource
{
    /// <summary>Faults tolerated before the layer is disabled. The second one disables.</summary>
    public const int MaxFaults = 2;

    private readonly IDxgiAdapters _dxgi;
    private readonly IAdapterMemoryCounter _memory;
    private readonly TimeProvider _clock;
    private readonly Lock _lock = new();

    private GpuAdapterIdentity[] _identities = [];
    private int _selected = -1;
    private bool _counterOpen;
    private bool _started;
    private bool _disposed;
    private int _capabilities;
    private int _faults;
    private int _disabled;
    private string? _lastFault;

    public BaselineTelemetrySource(IDxgiAdapters dxgi, IAdapterMemoryCounter memory, TimeProvider clock)
    {
        _dxgi = dxgi ?? throw new ArgumentNullException(nameof(dxgi));
        _memory = memory ?? throw new ArgumentNullException(nameof(memory));
        _clock = clock ?? throw new ArgumentNullException(nameof(clock));
    }

    public TelemetryLayer Layer => TelemetryLayer.Baseline;

    public GpuCapabilities Capabilities => (GpuCapabilities)Volatile.Read(ref _capabilities);

    public bool IsDisabled => Volatile.Read(ref _disabled) != 0;

    /// <summary>Faults counted so far. Two disables the layer.</summary>
    public int Faults => Volatile.Read(ref _faults);

    /// <summary>What the most recent fault said, for the report. Null until one happens.</summary>
    public string? LastFault => Volatile.Read(ref _lastFault);

    /// <summary>Every adapter DXGI listed, in its high-performance order. Empty until <see cref="Start"/>.</summary>
    public IReadOnlyList<GpuAdapterIdentity> Adapters => Volatile.Read(ref _identities);

    /// <summary>The adapter samples describe. Null before <see cref="Start"/> or on a machine with none.</summary>
    public GpuAdapterIdentity? Selected
    {
        get
        {
            lock (_lock)
            {
                return _selected >= 0 ? _identities[_selected] : null;
            }
        }
    }

    /// <summary>
    /// Enumerate the adapters, pick the default and bind its counter. Idempotent;
    /// <see cref="TryRead"/> calls it if nobody did. An enumeration that throws disables the
    /// layer at once: there is nothing to read.
    /// </summary>
    public void Start()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        lock (_lock)
        {
            if (_started || IsDisabled)
            {
                return;
            }

            _started = true;
            try
            {
                Volatile.Write(ref _identities, [.. _dxgi.Enumerate()]);
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                RecordFault($"Enumerate: {ex.GetType().Name}: {ex.Message}");
                Disable();
                return;
            }

            int first = Array.FindIndex(_identities, a => !a.IsSoftware);
            if (first >= 0)
            {
                Select(first);
            }
        }
    }

    /// <summary>
    /// Switch to the adapter the Overlay's handshake named. False — and no change — for a LUID
    /// DXGI did not list, or for 0, which the handshake carries until the first present.
    /// </summary>
    public bool SelectAdapter(ulong luid)
    {
        if (luid == 0)
        {
            return false;
        }

        Start();
        lock (_lock)
        {
            int i = Array.FindIndex(_identities, a => a.Luid == luid);
            if (i < 0)
            {
                return false;
            }

            if (i != _selected)
            {
                Select(i);
            }

            return true;
        }
    }

    public bool TryRead([NotNullWhen(true)] out GpuSample? sample)
    {
        sample = null;
        if (IsDisabled)
        {
            return false;
        }

        Start();
        lock (_lock)
        {
            if (IsDisabled || _selected < 0)
            {
                return false;
            }

            double? usedMb = null;
            if (_counterOpen)
            {
                try
                {
                    usedMb = _memory.ReadDedicatedUsageBytes() / 1048576.0;
                }
                catch (Exception ex) when (ex is not OperationCanceledException)
                {
                    RecordFault($"Dedicated Usage: {ex.GetType().Name}: {ex.Message}");
                    return false;
                }
            }

            sample = new GpuSample
            {
                TakenAt = _clock.GetUtcNow(),
                Layer = TelemetryLayer.Baseline,
                AdapterName = _identities[_selected].Name,
                VramAdapterMb = usedMb,
            };
        }

        MergeCapabilities(sample.PresentFields);
        return true;
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        lock (_lock)
        {
            _memory.Dispose();
            _counterOpen = false;
            _selected = -1;
        }
    }

    /// <summary>Under the lock. A counter that cannot be opened is N/A for the field, not a fault.</summary>
    private void Select(int index)
    {
        _selected = index;
        _counterOpen = _memory.TryOpen(_identities[index].Luid);
    }

    private void MergeCapabilities(GpuCapabilities present)
    {
        int seen;
        int merged;
        do
        {
            seen = Volatile.Read(ref _capabilities);
            merged = seen | (int)present;
        }
        while (Interlocked.CompareExchange(ref _capabilities, merged, seen) != seen);
    }

    private void RecordFault(string what)
    {
        Volatile.Write(ref _lastFault, what);
        if (Interlocked.Increment(ref _faults) >= MaxFaults)
        {
            Disable();
        }
    }

    private void Disable()
    {
        Volatile.Write(ref _disabled, 1);
        Volatile.Write(ref _capabilities, (int)GpuCapabilities.None);
    }
}
