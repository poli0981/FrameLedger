using FrameLedger.Application.Capture;
using FrameLedger.Application.Telemetry;
using FrameLedger.Shared;

namespace FrameLedger.Application.Recording;

/// <summary>
/// Keeps one session's <c>.partial</c> current: every flush interval and at every transition, whatever
/// the loop has drained since the last flush goes to the file, in the loop's order, and the file's
/// prefix stays a valid session at every byte (<c>04_CAPTURE</c> §Ring draining, 60 s crash-safety flush).
/// </summary>
/// <remarks>
/// Owned and driven by the recorder on the session loop's task — it is an <see cref="ICaptureObserver"/>
/// consumer, not a thread. It remembers how far into each live list it has written, so the loop stays
/// the only writer of the lists and this the only writer of the file.
/// </remarks>
public sealed class PartialSessionWriter : IDisposable
{
    public static readonly TimeSpan DefaultFlushInterval = TimeSpan.FromSeconds(60);

    private readonly IPartialSessionWriter _file;
    private readonly TimeProvider _clock;
    private readonly TimeSpan _interval;
    private readonly List<TelemetrySample> _sensors = [];

    private int _recordsWritten;
    private int _gapsWritten;
    private int _sensorsWritten;
    private int _touchesWritten;
    private long _lastFlushTimestamp;
    private bool _disposed;

    public PartialSessionWriter(IPartialSessionWriter file, TimeProvider clock, TimeSpan? flushInterval = null)
    {
        _file = file ?? throw new ArgumentNullException(nameof(file));
        _clock = clock ?? throw new ArgumentNullException(nameof(clock));
        _interval = flushInterval ?? DefaultFlushInterval;
        _lastFlushTimestamp = clock.GetTimestamp();
    }

    /// <summary>Flushes done so far, for tests and the report.</summary>
    public int Flushes { get; private set; }

    /// <summary>Telemetry the recorder drained from the poller, in order; flushed with the next tick.</summary>
    public IReadOnlyList<TelemetrySample> Sensors => _sensors;

    public void AddSensors(IEnumerable<TelemetrySample> samples)
    {
        ArgumentNullException.ThrowIfNull(samples);
        _sensors.AddRange(samples);
    }

    /// <summary>A state transition: written at once, never batched, because it is the breadcrumb.</summary>
    public void Note(string text)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        _file.AppendNote(_clock.GetUtcNow(), text);
    }

    /// <summary>Called per drain tick; flushes when the interval has elapsed, or always when <paramref name="force"/>.</summary>
    public bool OnTick(CaptureProgress progress, bool force = false)
    {
        ArgumentNullException.ThrowIfNull(progress);
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (!force && _clock.GetElapsedTime(_lastFlushTimestamp) < _interval)
        {
            return false;
        }

        Flush(progress);
        return true;
    }

    private void Flush(CaptureProgress progress)
    {
        if (progress.Records.Count > _recordsWritten)
        {
            FlFrameRecord[] delta = [.. progress.Records.Skip(_recordsWritten)];
            _file.AppendRecords(_recordsWritten, delta);
            _recordsWritten = progress.Records.Count;
        }

        if (progress.GapBefore.Count > _gapsWritten)
        {
            _file.AppendGaps([.. progress.GapBefore.Skip(_gapsWritten)]);
            _gapsWritten = progress.GapBefore.Count;
        }

        if (_sensors.Count > _sensorsWritten)
        {
            _file.AppendSensors([.. _sensors.Skip(_sensorsWritten)]);
            _sensorsWritten = _sensors.Count;
        }

        if (progress.TouchQpc.Count > _touchesWritten)
        {
            _file.AppendTouches([.. progress.TouchQpc.Skip(_touchesWritten)]);
            _touchesWritten = progress.TouchQpc.Count;
        }

        _file.AppendTick(new PartialTick(progress.DrainTicks, progress.ForegroundTicks, progress.TotalDropped,
            progress.TotalGaps, progress.GuardTicksPublished, _clock.GetUtcNow().ToUnixTimeMilliseconds(), progress.WriterState));
        _lastFlushTimestamp = _clock.GetTimestamp();
        Flushes++;
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _file.Dispose();
    }
}
