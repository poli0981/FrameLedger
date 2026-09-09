namespace FrameLedger.Application.Telemetry;

/// <summary>
/// The tokens <c>sessions.telemetry_source</c> is spelled in (<c>18_GPU_VENDOR_APIS</c>
/// §Abstraction: <c>l1+lhm+nvapi</c>). One place, so the writer and any reader agree.
/// </summary>
public static class TelemetryLayerNames
{
    public const string Separator = "+";

    public static string Of(TelemetryLayer layer) => layer switch
    {
        TelemetryLayer.Baseline => "l1",
        TelemetryLayer.Lhm => "lhm",
        TelemetryLayer.Nvapi => "nvapi",
        _ => throw new ArgumentOutOfRangeException(nameof(layer), layer, "not a telemetry layer"),
    };

    /// <summary>Lowest layer first, so the string reads as the ladder does.</summary>
    public static string Describe(IEnumerable<TelemetryLayer> standing)
    {
        ArgumentNullException.ThrowIfNull(standing);
        return string.Join(Separator, standing.Distinct().Order().Select(Of));
    }
}
