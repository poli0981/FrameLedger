using System.Text;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Consume;

/// <summary>
/// The Streamline buffer-type names behind <see cref="FlWriterState.SlTagCensus"/> bits, per route, for the report.
/// </summary>
/// <remarks>
/// Display only, like <see cref="CensusNames"/>. The authority is <c>FlSlTagType</c> in <c>fl_shm.h</c>; this
/// copy exists so a real-title run can say <i>which</i> buffers the title tagged on <i>which</i> route rather than
/// print a bit number — the input to §H5's question of whether a title feeds DLSS Frame Generation through the
/// exports this build hooks.
/// </remarks>
internal static class SlTagCensusNames
{
    private static readonly (FlSlTagType Bit, string Name)[] _names =
    [
        (FlSlTagType.Depth, "depth"),
        (FlSlTagType.MotionVectors, "mvec"),
        (FlSlTagType.Hudless, "hudless"),
        (FlSlTagType.ScalingInput, "scaling-in"),
        (FlSlTagType.ScalingOutput, "scaling-out"),
        (FlSlTagType.UiColorAlpha, "ui-color-alpha"),
        (FlSlTagType.UiAlpha, "ui-alpha"),
        (FlSlTagType.Backbuffer, "backbuffer"),
        (FlSlTagType.Other, "other"),
    ];

    /// <summary>Comma-separated names for one route's bits; <c>-</c> when none.</summary>
    public static string Describe(FlSlTagType bits)
    {
        var sb = new StringBuilder();
        foreach ((FlSlTagType bit, string name) in _names)
        {
            if (bits.HasFlag(bit))
            {
                if (sb.Length > 0)
                {
                    sb.Append(", ");
                }

                sb.Append(name);
            }
        }

        return sb.Length == 0 ? "-" : sb.ToString();
    }

    /// <summary>The whole census, one bracket per route.</summary>
    public static string DescribeRoutes(uint census) =>
        $"global=[{Describe(FlSlTagRoute.Of(census, FlSlTagRoute.Global))}]  "
        + $"frame=[{Describe(FlSlTagRoute.Of(census, FlSlTagRoute.Frame))}]  "
        + $"local=[{Describe(FlSlTagRoute.Of(census, FlSlTagRoute.Local))}]";
}
