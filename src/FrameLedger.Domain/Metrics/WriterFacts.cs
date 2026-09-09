namespace FrameLedger.Domain.Metrics;

/// <summary>
/// The session-level facts the calculators need from <c>FlWriterState</c>, with the sentinels already
/// resolved to <c>null</c> so no calculator can read "nobody wrote it" as a zero.
/// </summary>
/// <remarks>
/// <c>03_METRICS</c> §Inputs names them: <c>vramBudgetMb</c> for <c>budget_exceeded_pct</c>, <c>rtTier</c>
/// and <c>hooksInstalledMask</c> for the RT tri-state, <c>rtStateObjectsCreated</c> /
/// <c>rasterPsoCreated</c> for <c>pt_confidence</c>. The rest is carried for the report. A default
/// instance is an honest writer that installed nothing.
/// </remarks>
public sealed record WriterFacts
{
    /// <summary>A writer that installed nothing and measured nothing.</summary>
    public static WriterFacts Nothing { get; } = new();

    /// <summary><see cref="RtTierValue"/>'s domain: 0 not queried, 1 unsupported, else tier ×10.</summary>
    public uint RtTier { get; init; }

    public HookFamilies HooksInstalled { get; init; }

    public RuntimeCensusBits RuntimeCensus { get; init; }

    /// <summary>The raw Streamline tag census word; the route/type split lives with its consumer.</summary>
    public uint SlTagCensus { get; init; }

    public uint DxgiPresentsUnseen { get; init; }

    public uint DxgiPresentSamples { get; init; }

    /// <summary>Null when the writer published 0 — there is no producer yet, and 0 means "nobody wrote it".</summary>
    public uint? VramBudgetMb { get; init; }

    public uint RtStateObjectsCreated { get; init; }

    public uint RasterPsoCreated { get; init; }

    public uint FaultCount { get; init; }

    /// <summary>Null until the hook saw a present (the all-bits-set sentinel).</summary>
    public uint? DxgiPresentsBeforeHook { get; init; }

    /// <summary><c>rtTier ≥ 10</c>: an RT-capable device answered. Excludes both "not queried" and "unsupported".</summary>
    public bool RtCapable => RtTier >= (uint)RtTierValue.CapableMin;
}
