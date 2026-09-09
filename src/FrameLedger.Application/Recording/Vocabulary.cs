using FrameLedger.Domain.Metrics;
using FrameLedger.Shared;

namespace FrameLedger.Application.Recording;

/// <summary>
/// The tokens <c>sessions</c> stores (<c>0001_init.sql</c> column comments and CHECKs), from the enums the
/// pipeline carries. One place, so the writer and every reader agree; never a display string.
/// </summary>
public static class Vocabulary
{
    public const string NotApplicable = "na";

    public const string Measured = "measured";

    /// <summary><c>upscaler</c>: none|dlss|fsr2|fsr3|fsr4|xess|nis|unknown, plus <c>fsr</c> for the SDK 2.x DLL that does not name its version.</summary>
    public static string Upscaler(FlUpscaler value) => value switch
    {
        FlUpscaler.Dlss => "dlss",
        FlUpscaler.Fsr2 => "fsr2",
        FlUpscaler.Fsr3 => "fsr3",
        FlUpscaler.Fsr4 => "fsr4",
        FlUpscaler.XeSS => "xess",
        FlUpscaler.Nis => "nis",
        FlUpscaler.None => "none",
        FlUpscaler.FsrUnversioned => "fsr",
        _ => "unknown",
    };

    public static string Upscaler(UpscalerKind value) => Upscaler((FlUpscaler)(byte)value);

    /// <summary><c>fg_mode</c>: the technology's token, <c>none</c>, <c>active</c> for a counted but unidentified generator, else <c>na</c>.</summary>
    public static string FgMode(FgVerdict verdict, FlFgMode? identity) => verdict switch
    {
        FgVerdict.Named => identity switch
        {
            FlFgMode.DlssG => "dlssg",
            FlFgMode.FsrFg => "fsrfg",
            FlFgMode.XeFg => "xefg",
            FlFgMode.None => "none",
            _ => "unknown",
        },
        FgVerdict.None or FgVerdict.NoneInputsTagged => "none",
        FgVerdict.ActiveUnidentified => "active",
        _ => NotApplicable,
    };

    /// <summary><c>fg_source</c>: NULL = not measured; <c>api</c> a hooked identity; <c>cadence</c> the count alone; <c>none</c> the counted negative.</summary>
    public static string? FgSource(FgVerdict verdict) => verdict switch
    {
        FgVerdict.Named => "api",
        FgVerdict.ActiveUnidentified => "cadence",
        FgVerdict.None or FgVerdict.NoneInputsTagged => "none",
        _ => null,
    };

    public static string Tri(Tri value) => value switch
    {
        Domain.Metrics.Tri.Yes => "yes",
        Domain.Metrics.Tri.No => "no",
        _ => NotApplicable,
    };

    public static string? Api(FrameApi api) => api switch
    {
        FrameApi.D3D11 => "d3d11",
        FrameApi.D3D12 => "d3d12",
        FrameApi.Vulkan => "vulkan",
        FrameApi.OpenGL => "opengl",
        _ => null,
    };
}
