namespace FrameLedger.Application.Recording;

/// <summary>The bits of <c>frame_blobs.frame_flags</c> (<c>06_DATA_MODEL</c>: generated / dropped / gap).</summary>
[Flags]
public enum FrameFlagBits
{
    None = 0,

    /// <summary>
    /// A present that carried NO application-frame evaluation — set only where frame-generation counts
    /// were measured (<c>03_METRICS</c> §Export schema, <c>native_or_generated</c>); never guessed.
    /// </summary>
    Generated = 1 << 0,

    /// <summary>Reserved: nothing in the pipeline produces a per-frame drop today; ring drops are counted on the session.</summary>
    Dropped = 1 << 1,

    /// <summary>
    /// The interval INTO this frame is not a frame time: a torn slot or an overwrite skip preceded it, or
    /// it is the first record of its stream. The statistics exclude it; the export keeps it so
    /// <c>qpc_ms</c> stays reconstructible.
    /// </summary>
    Gap = 1 << 2,
}
