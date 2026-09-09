namespace FrameLedger.Domain.Metrics;

/// <summary>
/// Mirror of <c>FlHookFamily</c>: which hook families a writer installed. Monotonic — bits are only ever
/// set. Ray tracing's definite "No" requires <see cref="RtAsBuild"/> to have been <i>installed</i>, not
/// merely for RT to have been "measured".
/// </summary>
[Flags]
public enum HookFamilies
{
    None = 0,
    Present = 1 << 0,
    UpscalerIdentity = 1 << 1,
    UpscalerParams = 1 << 2,
    FgEvaluations = 1 << 3,
    RtDispatch = 1 << 4,
    RtAsBuild = 1 << 5,
    RtPso = 1 << 6,
    Pso = 1 << 7,
    ColorSpace = 1 << 8,
    Reflex = 1 << 9,
    Vram = 1 << 10,
}
