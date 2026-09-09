using Windows.Win32;

namespace FrameLedger.Infrastructure.Time;

/// <summary>
/// <c>QueryPerformanceCounter</c> / <c>QueryPerformanceFrequency</c>, by name — the clock every
/// <c>FlFrameRecord</c> is stamped in, and the pair a session stores as
/// <c>qpc_epoch</c> / <c>qpc_frequency</c> (<c>06_DATA_MODEL</c>).
/// </summary>
/// <remarks>
/// <see cref="System.Diagnostics.Stopwatch.GetTimestamp"/> and
/// <see cref="TimeProvider.GetTimestamp"/> are this same counter on Windows, and the managed
/// pipeline uses them; this type exists so that claim is <i>checked</i> (<c>QpcClockTests</c>)
/// rather than remembered, and so the recorder has a name for the frequency it stores.
/// </remarks>
public static class QpcClock
{
    /// <summary>Ticks per second. Constant for the life of the machine.</summary>
    public static long Frequency { get; } = ReadFrequency();

    /// <summary>The counter, now.</summary>
    public static unsafe long Now()
    {
        long ticks;
        PInvoke.QueryPerformanceCounter(&ticks);
        return ticks;
    }

    private static unsafe long ReadFrequency()
    {
        long hz;
        PInvoke.QueryPerformanceFrequency(&hz);
        return hz;
    }
}
