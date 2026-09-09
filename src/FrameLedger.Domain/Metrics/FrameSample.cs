using System.Runtime.InteropServices;

namespace FrameLedger.Domain.Metrics;

/// <summary>
/// One present, as the metric calculators see it: every field of <c>FlFrameRecord</c> that
/// <c>03_METRICS</c> §Inputs consumes — all of them except the two protocol fields <c>seq</c> and
/// <c>reserved</c> — with the enums Domain owns.
/// </summary>
/// <remarks>
/// <para>
/// <b>Domain references nothing</b> (CLAUDE.md §Solution layout), so this is not the shared-memory
/// record and must not become one: <c>FrameLedger.Application.Metrics.FrameSampleMapper</c> is the
/// single place a record is turned into a sample, and <c>MetricEnumMirrorTests</c> pins every enum
/// here to its <c>FrameLedger.Shared</c> twin in both directions.
/// </para>
/// <para>
/// Value-initialised like the record it mirrors: a default sample claims nothing
/// (<see cref="Measured"/> is <see cref="MeasuredFields.None"/>) and every enum's zero is "nobody
/// said", never a measured negative.
/// </para>
/// </remarks>
[StructLayout(LayoutKind.Auto)]
public readonly record struct FrameSample
{
    /// <summary>Present entry timestamp, QPC ticks.</summary>
    public ulong Qpc { get; init; }

    /// <summary>Process-wide present counter — NOT per stream, so gaps cannot be derived from it.</summary>
    public uint FrameIndex { get; init; }

    /// <summary>Stable per-swapchain id; 0 = unidentified, "one undifferentiated stream", never a valid id.</summary>
    public uint SwapchainId { get; init; }

    public FrameApi Api { get; init; }

    public uint PresentFlags { get; init; }

    public ushort SyncInterval { get; init; }

    public ushort RenderW { get; init; }

    public ushort RenderH { get; init; }

    public ushort OutputW { get; init; }

    public ushort OutputH { get; init; }

    public UpscalerKind Upscaler { get; init; }

    /// <summary>Vendor enum; <c>0xFF</c> unknown.</summary>
    public byte UpscalerQuality { get; init; }

    /// <summary>Percent, 0-100; <c>0xFF</c> means the API reports no sharpness.</summary>
    public byte UpscalerSharpness { get; init; }

    public FgKind FgMode { get; init; }

    /// <summary>Application-frame tokens drained by this present; saturates at 255.</summary>
    public byte FgEvaluations { get; init; }

    /// <summary>Presents DXGI counted on this chain since the previous hooked present; saturates at 255.</summary>
    public byte DxgiUnseen { get; init; }

    public RtEvidenceBits Rt { get; init; }

    /// <summary>Sum of W×H×D this frame — a VOLUME, not a call count; saturates at <see cref="uint.MaxValue"/>.</summary>
    public uint DispatchRaysVolume { get; init; }

    public byte MaxTraceRecursionDepth { get; init; }

    /// <summary>Compile COUNT, not a flag.</summary>
    public ushort PsoCreated { get; init; }

    public FeatureBits Features { get; init; }

    public ColorSpaceKind ColorSpace { get; init; }

    /// <summary>Per-process VRAM in MiB, a 1 Hz held sample.</summary>
    public uint VramUsedMb { get; init; }

    /// <summary>0 = unavailable.</summary>
    public uint ReflexLatencyUs { get; init; }

    public MeasuredFields Measured { get; init; }

    /// <summary>True when every bit of <paramref name="fields"/> is claimed.</summary>
    public bool Claims(MeasuredFields fields) => (Measured & fields) == fields;
}
