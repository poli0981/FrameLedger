using System.Collections.Concurrent;
using FrameLedger.Application.Telemetry;

namespace FrameLedger.Infrastructure.Telemetry;

/// <summary>
/// The <c>fl-telemetry</c> thread: one <see cref="IGpuTelemetrySource.TryRead"/> per interval,
/// stamped with QPC, queued for the session loop (<see cref="ITelemetryPoller"/>).
/// </summary>
/// <remarks>
/// <para>
/// <b>The stamp is <see cref="TimeProvider.GetTimestamp"/>.</b> On Windows that is
/// <c>QueryPerformanceCounter</c> — the clock the Overlay writes into every record — and
/// <c>QpcClockTests</c> pins the equality against the real counter rather than trusting the
/// documentation, because a sensor series in a different clock domain from the frames it is
/// laid beside is wrong in a way no aggregate would reveal.
/// </para>
/// <para>
/// <b>Every tick is queued, duplicates included.</b> A layer that publishes at its own 1 Hz
/// may be read twice with the same value; the series records what the poller saw each
/// second, and de-duplicating on the merged sample's <see cref="GpuSample.TakenAt"/> would
/// discard a lower layer's fresh field whenever the top layer's had not moved.
/// </para>
/// <para>
/// Does not own the source unless told so at construction: by default the composition root that built
/// the layers disposes them.
/// </para>
/// </remarks>
public sealed class TelemetryPoller : ITelemetryPoller
{
    private readonly IGpuTelemetrySource _source;
    private readonly TelemetryPollerOptions _options;
    private readonly TimeProvider _clock;
    private readonly ConcurrentQueue<TelemetrySample> _queue = new();
    private readonly ManualResetEventSlim _stop = new(false);

    private readonly bool _ownsSource;

    private Thread? _thread;
    private int _queued;
    private long _dropped;
    private bool _disposed;

    /// <summary>A poller over <paramref name="source"/>, reading every <paramref name="options"/>.Interval on <paramref name="clock"/>.</summary>
    /// <param name="source">The composite (or single layer) to read.</param>
    /// <param name="options">Cadence and queue bound.</param>
    /// <param name="clock">Stamps samples with its <see cref="TimeProvider.GetTimestamp"/>, which is QPC.</param>
    /// <param name="ownsSource">
    /// True when the poller is the source's only owner and should dispose it — a composition root that
    /// builds the layers for one session and hands them over. False (the default) when the container owns them.
    /// </param>
    public TelemetryPoller(IGpuTelemetrySource source, TelemetryPollerOptions options, TimeProvider clock, bool ownsSource = false)
    {
        _source = source ?? throw new ArgumentNullException(nameof(source));
        _ownsSource = ownsSource;
        _options = options ?? throw new ArgumentNullException(nameof(options));
        _clock = clock ?? throw new ArgumentNullException(nameof(clock));
        if (options.Interval < TelemetryPollerOptions.MinimumInterval)
        {
            throw new ArgumentOutOfRangeException(nameof(options), options.Interval,
                "18_GPU_VENDOR_APIS §Runtime policy: 1 Hz, never faster than the layers answer");
        }

        ArgumentOutOfRangeException.ThrowIfLessThan(options.QueueCapacity, 1, nameof(options));
    }

    public string Descriptor => _source switch
    {
        CompositeTelemetrySource composite => composite.Descriptor,
        _ when _source.IsDisabled => string.Empty,
        _ => TelemetryLayerNames.Of(_source.Layer),
    };

    public long Dropped => Volatile.Read(ref _dropped);

    /// <summary>Samples waiting for <see cref="Drain"/>.</summary>
    public int Queued => Volatile.Read(ref _queued);

    public void Start()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_thread is not null)
        {
            return;
        }

        _thread = new Thread(Loop)
        {
            IsBackground = true,
            Name = "fl-telemetry",
        };
        _thread.Start();
    }

    /// <summary>
    /// One tick: read the source, stamp, queue. Public so a test can drive the poller without
    /// its thread; the thread calls exactly this.
    /// </summary>
    public void PollOnce()
    {
        if (!_source.TryRead(out GpuSample? sample))
        {
            return;
        }

        _queue.Enqueue(new TelemetrySample(_clock.GetTimestamp(), sample));
        if (Interlocked.Increment(ref _queued) > _options.QueueCapacity && _queue.TryDequeue(out _))
        {
            Interlocked.Decrement(ref _queued);
            Interlocked.Increment(ref _dropped);
        }
    }

    public int Drain(ICollection<TelemetrySample> into)
    {
        ArgumentNullException.ThrowIfNull(into);
        int n = 0;
        while (_queue.TryDequeue(out TelemetrySample sample))
        {
            Interlocked.Decrement(ref _queued);
            into.Add(sample);
            n++;
        }

        return n;
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _stop.Set();
        _thread?.Join(TimeSpan.FromSeconds(2));
        _stop.Dispose();
        if (_ownsSource)
        {
            _source.Dispose();
        }
    }

    private void Loop()
    {
        do
        {
            PollOnce();
        }
        while (!_stop.Wait(_options.Interval));
    }
}
