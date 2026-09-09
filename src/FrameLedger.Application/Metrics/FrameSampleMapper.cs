using FrameLedger.Domain.Metrics;
using FrameLedger.Shared;

namespace FrameLedger.Application.Metrics;

/// <summary>
/// The one place a shared-memory record becomes a <see cref="FrameSample"/>, and a writer state a
/// <see cref="WriterFacts"/>.
/// </summary>
/// <remarks>
/// <para>
/// Domain references nothing (CLAUDE.md §Solution layout), so it cannot see <c>FlFrameRecord</c>; this
/// assembly references both and does the copy field by field. The enums are cast, not translated —
/// <c>MetricEnumMirrorTests</c> asserts every Domain enum is numerically identical to its
/// <c>FrameLedger.Shared</c> twin in both directions, so a cast is exact and a member added on one side
/// alone fails the mirror rather than silently mapping to 0.
/// </para>
/// <para>
/// <b>The sentinels are resolved here, once.</b> <c>VramBudgetMb == 0</c> is "nobody wrote it" and
/// <c>DxgiPresentsBeforeHook == 0xFFFFFFFF</c> is "no present seen": both become null so no calculator
/// downstream can read them as a measured zero.
/// </para>
/// </remarks>
public static class FrameSampleMapper
{
    public static FrameSample Map(FlFrameRecord r) => new()
    {
        Qpc = r.Qpc,
        FrameIndex = r.FrameIndex,
        SwapchainId = r.SwapchainId,
        Api = (FrameApi)r.Api,
        PresentFlags = r.PresentFlags,
        SyncInterval = r.SyncInterval,
        RenderW = r.RenderW,
        RenderH = r.RenderH,
        OutputW = r.OutputW,
        OutputH = r.OutputH,
        Upscaler = (UpscalerKind)r.Upscaler,
        UpscalerQuality = r.UpscalerQuality,
        UpscalerSharpness = r.UpscalerSharpness,
        FgMode = (FgKind)r.FgMode,
        FgEvaluations = r.FgEvaluations,
        DxgiUnseen = r.DxgiUnseen,
        Rt = (RtEvidenceBits)r.RtFlags,
        DispatchRaysVolume = r.DispatchRaysVolume,
        MaxTraceRecursionDepth = r.MaxTraceRecursionDepth,
        PsoCreated = r.PsoCreatedThisFrame,
        Features = (FeatureBits)r.FeatureFlags,
        ColorSpace = (ColorSpaceKind)r.ColorSpace,
        VramUsedMb = r.VramUsedMb,
        ReflexLatencyUs = r.ReflexLatencyUs,
        Measured = (MeasuredFields)r.MeasuredMask,
    };

    /// <summary>Every record, in order.</summary>
    public static IReadOnlyList<FrameSample> Map(IReadOnlyList<FlFrameRecord> records)
    {
        ArgumentNullException.ThrowIfNull(records);

        var samples = new FrameSample[records.Count];
        for (int i = 0; i < samples.Length; i++)
        {
            samples[i] = Map(records[i]);
        }

        return samples;
    }

    public static WriterFacts Map(FlWriterState w) => new()
    {
        RtTier = w.RtTier,
        HooksInstalled = (HookFamilies)w.HooksInstalledMask,
        RuntimeCensus = (RuntimeCensusBits)w.RuntimeCensus,
        SlTagCensus = w.SlTagCensus,
        DxgiPresentsUnseen = w.DxgiPresentsUnseen,
        DxgiPresentSamples = w.DxgiPresentSamples,
        VramBudgetMb = w.VramBudgetMb == 0 ? null : w.VramBudgetMb,
        RtStateObjectsCreated = w.RtStateObjectsCreated,
        RasterPsoCreated = w.RasterPsoCreated,
        FaultCount = w.FaultCount,
        DxgiPresentsBeforeHook = w.DxgiPresentsBeforeHook == FlWriterState.DxgiPresentsBeforeHookNotRead
            ? null
            : w.DxgiPresentsBeforeHook,
    };
}
