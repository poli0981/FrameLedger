namespace FrameLedger.CaptureHost.Consume;

/// <summary>
/// The bits of <c>NV_NGX_DLSS_OVERRIDE_BITFIELD</c> (vendored <c>nvapi.h</c>, R570+), as the driver reports them per
/// process through <c>NvAPI_NGX_GetNGXOverrideState</c>. Constants rather than a <c>[Flags]</c> enum because the
/// masks are 64-bit words the probe prints raw, and a word is what the parser keeps.
/// </summary>
/// <remarks>
/// <b>Measured 2026-09-06 (driver 616.64, <c>20_OPEN_QUESTIONS</c> §H5):</b> <see cref="Created"/> and
/// <see cref="Evaluate"/> on the super-resolution word are set for a process with NO NVIDIA-app override
/// (Hell Is Us, Lies of P), and CLEAR on Lies of P with upscaling switched off — the negative that keeps the rung.
/// <b>The frame-generation word follows DLSS-G too, once the feature exists</b> — the morning's
/// <see cref="Initialized"/> | <see cref="DllExists"/> on Hell Is Us was read before creation; the captures beside
/// the loop read <see cref="Created"/> | <see cref="Evaluate"/> on Hell Is Us, Onimusha and Dying Light: The Beast at
/// ×4, with the word CHANGING between the session's readings. <see cref="FgMode"/> and <see cref="FgMultiFrame"/>
/// stay clear at ×4 and <c>frameGenerationCount</c> is 0, so the word names the feature and never the multiplier.
/// </remarks>
internal static class NgxOverrideFlags
{
    public const ulong Initialized = 1UL << 0;
    public const ulong Enabled = 1UL << 1;
    public const ulong DllExists = 1UL << 2;
    public const ulong DllLoaded = 1UL << 3;
    public const ulong DllSelected = 1UL << 4;
    public const ulong Preset = 1UL << 5;
    public const ulong PerfMode = 1UL << 6;
    public const ulong ScalingRatio = 1UL << 7;
    public const ulong OptimalSettings = 1UL << 8;
    public const ulong Created = 1UL << 9;
    public const ulong Evaluate = 1UL << 10;
    public const ulong FgMode = 1UL << 13;
    public const ulong SrDlaaMode = 1UL << 14;
    public const ulong FgMultiFrame = 1UL << 15;
    public const ulong ErrFailed = 1UL << 16;
    public const ulong ErrDenied = 1UL << 17;
    public const ulong ErrDrs = 1UL << 18;
    public const ulong ErrNotFound = 1UL << 19;
    public const ulong ErrDllLoad = 1UL << 20;

    /// <summary>The two bits that together say "this feature ran in this process": created, and evaluated.</summary>
    public const ulong CreatedAndEvaluated = Created | Evaluate;

    private static readonly (ulong Bit, string Name)[] _names =
    [
        (Initialized, "INITIALIZED"), (Enabled, "ENABLED"), (DllExists, "DLL_EXISTS"), (DllLoaded, "DLL_LOADED"),
        (DllSelected, "DLL_SELECTED"), (Preset, "PRESET"), (PerfMode, "PERF_MODE"), (ScalingRatio, "SCALING_RATIO"),
        (OptimalSettings, "OPTIMAL_SETTINGS"), (Created, "CREATED"), (Evaluate, "EVALUATE"), (FgMode, "FG_MODE"),
        (SrDlaaMode, "SR_DLAA_MODE"), (FgMultiFrame, "FG_MULTI_FRAME"), (ErrFailed, "ERR_FAILED"),
        (ErrDenied, "ERR_DENIED"), (ErrDrs, "ERR_DRS"), (ErrNotFound, "ERR_NOT_FOUND"), (ErrDllLoad, "ERR_DLL_LOAD"),
    ];

    /// <summary>The probe's own spelling of a word's bits, unknown bits printed as hex so nothing is dropped.</summary>
    public static string Describe(ulong mask)
    {
        if (mask == 0)
        {
            return "-";
        }

        List<string> parts = [];
        ulong seen = 0;
        foreach ((ulong bit, string name) in _names)
        {
            if ((mask & bit) != 0)
            {
                parts.Add(name);
                seen |= bit;
            }
        }

        ulong unknown = mask & ~seen;
        if (unknown != 0)
        {
            parts.Add("0x" + unknown.ToString("X", System.Globalization.CultureInfo.InvariantCulture));
        }

        return string.Join(' ', parts);
    }
}
