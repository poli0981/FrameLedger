using System.Diagnostics.CodeAnalysis;
using FrameLedger.Application.Telemetry;
using LibreHardwareMonitor.Hardware;

namespace FrameLedger.Infrastructure.Telemetry;

/// <summary>
/// L2 — adapter-wide GPU sensors from LibreHardwareMonitorLib
/// (<c>docs/18_GPU_VENDOR_APIS.md</c> §L2).
/// </summary>
/// <remarks>
/// <para>
/// <b>The first telemetry source in the tree, and it exists because §M5 could not be
/// measured without one.</b> The two-rung ladder promises a Tier-2 session <i>"whatever
/// hardware telemetry this machine can provide"</i>, and whether an unelevated Agent gets
/// GPU sensors at all was a sentence in three user-facing documents with no measurement
/// behind it. This class is the instrument; <c>20_OPEN_QUESTIONS</c> §M5 carries the
/// decision table that was written before it ran.
/// </para>
/// <para>
/// <b>Polls on a thread of its own</b>, never on a caller's — the library's update walks
/// vendor driver paths that can block. The latest sample is handed over by reference swap,
/// so <see cref="TryRead"/> never waits on the poller. <b>Which thread this is in the Agent
/// is unspecified</b> (<c>20_OPEN_QUESTIONS</c> §G, threading model); here it is simply
/// "the one this object owns".
/// </para>
/// <para>
/// <b>Fault policy, from §Runtime policy: throws or hangs twice ⇒ disabled for the
/// session.</b> A throw is counted where it happens. A hang is counted by the reader: a
/// poll still in progress after <see cref="LhmTelemetryOptions.HangThreshold"/> is one
/// fault, counted once per poll. Disabled means <see cref="Capabilities"/> is
/// <see cref="GpuCapabilities.None"/> and <see cref="TryRead"/> is false, permanently — the
/// object is not retried in a loop, because a layer that fails twice in a row is one whose
/// third answer nobody should trust.
/// </para>
/// <para>
/// <b>Capabilities are monotonic and value-derived.</b> A bit is set the first time its
/// field carries a value and never cleared by a later null tick; a sensor that exists and
/// never reports sets nothing. <see cref="GpuSample.PresentFields"/> is the only input.
/// </para>
/// </remarks>
public sealed class LhmTelemetrySource : IGpuTelemetrySource
{
    /// <summary>Faults tolerated before the layer is disabled. The second one disables.</summary>
    public const int MaxFaults = 2;

    private readonly ILhmComputer _computer;
    private readonly LhmTelemetryOptions _options;
    private readonly TimeProvider _clock;
    private readonly ManualResetEventSlim _stop = new(false);
    private readonly Lock _faultLock = new();

    private Thread? _thread;
    private GpuSample? _latest;
    private int _capabilities;
    private int _faults;
    private int _disabled;
    private int _polling;
    private int _hangCounted;
    private long _pollStartedUtcTicks;
    private bool _opened;
    private bool _disposed;
    private string? _lastFault;

    public LhmTelemetrySource(ILhmComputer computer, LhmTelemetryOptions options, TimeProvider clock)
    {
        _computer = computer ?? throw new ArgumentNullException(nameof(computer));
        _options = options ?? throw new ArgumentNullException(nameof(options));
        _clock = clock ?? throw new ArgumentNullException(nameof(clock));
        if (options.Interval < LhmTelemetryOptions.MinimumInterval)
        {
            throw new ArgumentOutOfRangeException(nameof(options), options.Interval,
                "18_GPU_VENDOR_APIS §L2: never poll LibreHardwareMonitor faster than 500 ms");
        }
    }

    public TelemetryLayer Layer => TelemetryLayer.Lhm;

    public GpuCapabilities Capabilities => (GpuCapabilities)Volatile.Read(ref _capabilities);

    /// <summary>Faults counted so far. Two disables the layer.</summary>
    public int Faults => Volatile.Read(ref _faults);

    /// <summary>True once the layer has been disabled for the session. Never clears.</summary>
    public bool IsDisabled => Volatile.Read(ref _disabled) != 0;

    /// <summary>True while a poll is executing on the poller thread.</summary>
    public bool IsPolling => Volatile.Read(ref _polling) != 0;

    /// <summary>What the most recent fault said, for the report. Null until one happens.</summary>
    public string? LastFault => Volatile.Read(ref _lastFault);

    /// <summary>
    /// Open the library and start polling. Idempotent. An <c>Open()</c> that throws disables
    /// the layer at once: there is nothing to poll.
    /// </summary>
    public void Start()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_thread is not null || IsDisabled)
        {
            return;
        }

        try
        {
            _computer.Open();
            _opened = true;
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            RecordFault($"Open: {ex.GetType().Name}: {ex.Message}");
            Disable();
            return;
        }

        _thread = new Thread(Loop)
        {
            IsBackground = true,
            Name = "FrameLedger.Lhm",
        };
        _thread.Start();
    }

    /// <summary>
    /// One poll: refresh the tree, map the first GPU node, publish. Public so a probe or a
    /// test can drive it without the thread; the thread calls exactly this.
    /// </summary>
    public void PollOnce()
    {
        if (IsDisabled)
        {
            return;
        }

        Volatile.Write(ref _hangCounted, 0);
        Volatile.Write(ref _pollStartedUtcTicks, _clock.GetUtcNow().UtcTicks);
        Volatile.Write(ref _polling, 1);
        try
        {
            _computer.Update();

            IHardware? gpu = null;
            foreach (IHardware hardware in _computer.Hardware)
            {
                if (SensorMap.IsGpu(hardware))
                {
                    gpu = hardware;
                    break;
                }
            }

            if (gpu is null)
            {
                // No GPU node is not a fault: LHM opened, walked, and found no vendor it
                // understands. That is an answer — N/A with the layer still healthy — and
                // the probe reports it as its own row rather than folding it into R3.
                Volatile.Write(ref _latest, null);
                return;
            }

            GpuSample sample = SensorMap.Build(gpu, _clock.GetUtcNow());
            Volatile.Write(ref _latest, sample);

            int present = (int)sample.PresentFields;
            int seen;
            int merged;
            do
            {
                seen = Volatile.Read(ref _capabilities);
                merged = seen | present;
            }
            while (Interlocked.CompareExchange(ref _capabilities, merged, seen) != seen);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            RecordFault($"Update: {ex.GetType().Name}: {ex.Message}");
        }
        finally
        {
            Volatile.Write(ref _polling, 0);
        }
    }

    public bool TryRead([NotNullWhen(true)] out GpuSample? sample)
    {
        sample = null;
        if (IsDisabled)
        {
            return false;
        }

        if (IsPolling)
        {
            DateTimeOffset started = new(Volatile.Read(ref _pollStartedUtcTicks), TimeSpan.Zero);
            if (_clock.GetUtcNow() - started > _options.HangThreshold
                && Interlocked.Exchange(ref _hangCounted, 1) == 0)
            {
                RecordFault($"poll exceeded {_options.HangThreshold.TotalSeconds:0.#} s");
                if (IsDisabled)
                {
                    return false;
                }
            }
        }

        sample = Volatile.Read(ref _latest);
        return sample is not null;
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _stop.Set();

        bool returned = _thread is null || _thread.Join(TimeSpan.FromSeconds(2));
        if (returned && _opened)
        {
            try
            {
                _computer.Close();
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                // A Close() that throws at teardown changes nothing the session recorded.
                Volatile.Write(ref _lastFault, $"Close: {ex.GetType().Name}: {ex.Message}");
            }
        }

        // A thread that did not return is hung inside the library. It is a background
        // thread, so it cannot hold the process open, and Close() is not called under it:
        // tearing down the library's state beneath a blocked driver call is worse than
        // leaking it.
        _stop.Dispose();
    }

    private void Loop()
    {
        do
        {
            PollOnce();
        }
        while (!IsDisabled && !_stop.Wait(_options.Interval));
    }

    private void RecordFault(string what)
    {
        lock (_faultLock)
        {
            Volatile.Write(ref _lastFault, what);
            if (Interlocked.Increment(ref _faults) >= MaxFaults)
            {
                Disable();
            }
        }
    }

    private void Disable()
    {
        Volatile.Write(ref _disabled, 1);
        Volatile.Write(ref _capabilities, (int)GpuCapabilities.None);
        Volatile.Write(ref _latest, null);
        _stop.Set();
    }
}
