using System.Text;
using FrameLedger.Shared;

namespace FrameLedger.Application.Capture;

/// <summary>
/// The module names behind <see cref="FlRuntimeCensus"/> bits, for the report.
/// </summary>
/// <remarks>
/// Display only. The authoritative table is <c>FL_RUNTIME_CENSUS</c> in
/// <c>fl_hook_inventory.h</c>, gated by <c>hookinventory-check</c> Pass D against the measured
/// module list; this copy exists so a WARNING can say <i>which</i> runtime was loaded rather
/// than print a bit number. A bit with no name here prints as its enum member, never as
/// nothing.
/// </remarks>
public static class CensusNames
{
    private static readonly (FlRuntimeCensus Bit, string Name)[] _names =
    [
        (FlRuntimeCensus.SlDlssG, "sl.dlss_g.dll"),
        (FlRuntimeCensus.NvngxDlssG, "nvngx_dlssg.dll"),
        (FlRuntimeCensus.LibXessFg, "libxess_fg.dll"),
        (FlRuntimeCensus.FfxFrameInterpolation, "ffx_frameinterpolation_x64.dll"),
        (FlRuntimeCensus.FfxFsr3, "ffx_fsr3_x64.dll"),
        (FlRuntimeCensus.AmdFfxFrameGeneration, "amd_fidelityfx_framegeneration_dx12.dll"),
        (FlRuntimeCensus.SlInterposer, "sl.interposer.dll"),
        (FlRuntimeCensus.SlDlss, "sl.dlss.dll"),
        (FlRuntimeCensus.SlNis, "sl.nis.dll"),
        (FlRuntimeCensus.NvngxCore, "nvngx.dll / _nvngx.dll"),
        (FlRuntimeCensus.NvngxDlss, "nvngx_dlss.dll"),
        (FlRuntimeCensus.NvngxDlssD, "nvngx_dlssd.dll"),
        (FlRuntimeCensus.LibXess, "libxess.dll / libxess_dx11.dll"),
        (FlRuntimeCensus.FfxFsr2, "ffx_fsr2_api_x64.dll / ffx_fsr2_api_dx12_x64.dll"),
        (FlRuntimeCensus.FfxFsr3Upscaler, "ffx_fsr3upscaler_x64.dll"),
        (FlRuntimeCensus.AmdFfxUpscaler, "amd_fidelityfx_upscaler_dx12.dll"),
        (FlRuntimeCensus.AmdFfxDx12, "amd_fidelityfx_dx12.dll"),
    ];

    /// <summary>
    /// Every module FILE name the census can answer for — the <c>a / b</c> pairs above split into
    /// their members — so a module snapshot asks about exactly the set the census names.
    /// </summary>
    public static IReadOnlyList<string> ModuleFileNames { get; } =
        _names.SelectMany(n => n.Name.Split(" / ", StringSplitOptions.RemoveEmptyEntries)).ToArray();

    /// <summary>Comma-separated module names for the set family bits; <c>-</c> when none.</summary>
    public static string Describe(FlRuntimeCensus bits)
    {
        var sb = new StringBuilder();
        FlRuntimeCensus named = FlRuntimeCensus.None;
        foreach ((FlRuntimeCensus bit, string name) in _names)
        {
            if (bits.HasFlag(bit))
            {
                Append(sb, name);
                named |= bit;
            }
        }

        FlRuntimeCensus unnamed = bits & ~named & ~FlRuntimeCensus.Ran;
        if (unnamed != FlRuntimeCensus.None)
        {
            Append(sb, unnamed.ToString());
        }

        return sb.Length == 0 ? "-" : sb.ToString();
    }

    private static void Append(StringBuilder sb, string name)
    {
        if (sb.Length > 0)
        {
            sb.Append(", ");
        }

        sb.Append(name);
    }
}
