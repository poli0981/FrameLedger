namespace FrameLedger.Infrastructure.Recording;

/// <summary>The chunk types of a <c>.partial</c> (<c>06_DATA_MODEL</c> §The <c>.partial</c> file). Numbers are on disk; never renumber.</summary>
internal enum PartialChunkType
{
    None = 0,

    /// <summary>UTF-8 JSON of <c>PartialHeader</c>. Always the first chunk.</summary>
    Header = 1,

    /// <summary><c>i64 firstOrdinal</c> then N raw <c>FlFrameRecord</c>s (64 bytes each).</summary>
    Records = 2,

    /// <summary>N × <c>i32</c> gap-before record indices.</summary>
    Gaps = 3,

    /// <summary>N × one telemetry sample (<c>SensorSampleCodec</c>).</summary>
    Sensors = 4,

    /// <summary>The drain's accounting and the raw <c>FlWriterState</c>; the last one wins.</summary>
    Tick = 5,

    /// <summary><c>i64 unixMs</c> then UTF-8 text: a state transition.</summary>
    Note = 6,

    /// <summary>N × <c>i64</c> QPC ticks at which the host touched the target.</summary>
    Touches = 7,
}
