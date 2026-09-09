using System.Diagnostics.CodeAnalysis;
using FrameLedger.Application.Telemetry;

namespace FrameLedger.Infrastructure.Telemetry;

/// <summary>
/// L3 — NVIDIA extras through the bridge (<c>docs/18_GPU_VENDOR_APIS.md</c> §L3): throttle reasons,
/// vendor-reported load, clocks, temperatures, fan, adapter memory, PCIe width, the driver version.
/// </summary>
/// <remarks>
/// <para>
/// <b>Reads on the caller's thread, like L1</b>: a bridge read is a handful of NVAPI queries in
/// microseconds, so the poller thread calls <see cref="TryRead"/>. <b>Unavailable is not zero and not a
/// fault</b>: a machine with no NVIDIA driver, or a build without the DLL, disables the layer at
/// <see cref="Start"/> with <see cref="GpuCapabilities.None"/>, and the composite's descriptor drops
/// <c>nvapi</c>. A bridge that THROWS twice is disabled by the port's rule; an NVAPI call the driver
/// refuses only leaves its bit clear.
/// </para>
/// <para>
/// <b>Not latency.</b> Reflex / PC latency is per frame and in the ring (<c>20_OPEN_QUESTIONS</c> §M8);
/// this source never calls <c>NvAPI_D3D_GetLatency</c>.
/// </para>
/// </remarks>
public sealed class NvapiTelemetrySource : IGpuTelemetrySource
{
    /// <summary>Faults tolerated before the layer is disabled. The second one disables.</summary>
    public const int MaxFaults = 2;

    private readonly INvapiBridge _bridge;
    private readonly TimeProvider _clock;
    private readonly Lock _lock = new();

    private bool _started;
    private bool _initialised;
    private bool _disposed;
    private int _capabilities;
    private int _faults;
    private int _disabled;
    private string? _lastFault;
    private string? _driverVersion;

    public NvapiTelemetrySource(INvapiBridge bridge, TimeProvider clock)
    {
        _bridge = bridge ?? throw new ArgumentNullException(nameof(bridge));
        _clock = clock ?? throw new ArgumentNullException(nameof(clock));
    }

    /// <summary>Over the real DLL: the bridge is born owned here, so there is no hand-off to reason about.</summary>
    public NvapiTelemetrySource(TimeProvider clock)
    {
        _bridge = new NativeNvapiBridge();
        _clock = clock ?? throw new ArgumentNullException(nameof(clock));
    }

    public TelemetryLayer Layer => TelemetryLayer.Nvapi;

    public GpuCapabilities Capabilities => (GpuCapabilities)Volatile.Read(ref _capabilities);

    public bool IsDisabled => Volatile.Read(ref _disabled) != 0;

    public int Faults => Volatile.Read(ref _faults);

    public string? LastFault => Volatile.Read(ref _lastFault);

    /// <summary>The driver's marketing version (<c>616.64</c>), once started on an NVIDIA machine; null otherwise.</summary>
    public string? DriverVersion => Volatile.Read(ref _driverVersion);

    /// <summary>Initialise NVAPI through the bridge. Idempotent; a refusal disables the layer for the session.</summary>
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
            int status;
            try
            {
                status = _bridge.Init();
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                RecordFault($"Init: {ex.GetType().Name}: {ex.Message}");
                Disable();
                return;
            }

            if (status != 0)
            {
                Volatile.Write(ref _lastFault, status == NativeNvapiBridge.Unavailable
                    ? "the bridge DLL is not beside this binary"
                    : $"NvAPI_Initialize / enumeration answered {status}");
                Disable();
                return;
            }

            _initialised = true;
            if (_bridge.DriverVersion(out uint version, out string branch) == 0)
            {
                Volatile.Write(ref _driverVersion, $"{version / 100}.{version % 100:00} ({branch})");
            }
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
            if (IsDisabled || !_initialised)
            {
                return false;
            }

            NvapiSample raw;
            try
            {
                if (_bridge.ReadSample(out raw) != 0)
                {
                    return false;
                }
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                RecordFault($"ReadSample: {ex.GetType().Name}: {ex.Message}");
                return false;
            }

            sample = Map(raw, _clock.GetUtcNow());
        }

        MergeCapabilities(sample.PresentFields);
        return true;
    }

    private static GpuSample Map(NvapiSample r, DateTimeOffset takenAt) => new()
    {
        TakenAt = takenAt,
        Layer = TelemetryLayer.Nvapi,
        AdapterName = r.Has(NvapiField.Name) ? r.Name : null,
        TempCoreC = r.Has(NvapiField.TempCore) ? r.TempCoreC : null,
        TempMemoryC = r.Has(NvapiField.TempMemory) ? r.TempMemoryC : null,
        LoadPct = r.Has(NvapiField.Load) ? r.LoadPct : null,
        VramAdapterMb = r.Has(NvapiField.Vram) ? r.VramUsedMb : null,
        CoreClockMhz = r.Has(NvapiField.CoreClock) ? r.CoreClockMhz : null,
        MemClockMhz = r.Has(NvapiField.MemClock) ? r.MemClockMhz : null,
        FanRpm = r.Has(NvapiField.Fan) ? r.FanRpm : null,
        ThrottleReasons = r.Has(NvapiField.Throttle) ? r.ThrottleReasons : null,
        PcieWidth = r.Has(NvapiField.PcieWidth) ? (int)r.PcieWidth : null,
    };

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        lock (_lock)
        {
            if (_initialised)
            {
                _bridge.Shutdown();
                _initialised = false;
            }
        }

        _bridge.Dispose();
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
