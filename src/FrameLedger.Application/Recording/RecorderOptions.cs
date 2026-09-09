namespace FrameLedger.Application.Recording;

/// <summary>Knobs on <see cref="SessionRecorder"/>, so a test can flush every few milliseconds instead of every minute.</summary>
public sealed record RecorderOptions
{
    /// <summary><c>04_CAPTURE</c> §Ring draining: every 60 s, a crash-safety flush.</summary>
    public TimeSpan PartialFlushInterval { get; init; } = PartialSessionWriter.DefaultFlushInterval;

    public int RetentionKeep { get; init; } = SessionFinalizer.DefaultRetentionKeep;

    /// <summary>The discard rule's threshold; the Agent keeps the 30 s default.</summary>
    public TimeSpan MinimumSessionLength { get; init; } = SessionFinalizer.MinimumSessionLength;
}
