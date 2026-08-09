using System.Text.Json.Serialization;

namespace FrameLedger.CaptureHost.Consent;

/// <summary>One game's row on disk.</summary>
internal sealed record ConsentFileEntry
{
    [JsonPropertyName("exePath")]
    public string ExePath { get; init; } = string.Empty;

    [JsonPropertyName("sizeBytes")]
    public long SizeBytes { get; init; }

    [JsonPropertyName("mtimeUnixMs")]
    public long MtimeUnixMs { get; init; }

    [JsonPropertyName("hookEnabled")]
    public bool HookEnabled { get; init; }

    /// <summary>Unix ms UTC, or null. Never a local time (CLAUDE.md §Coding conventions).</summary>
    [JsonPropertyName("consentedAtUnixMs")]
    public long? ConsentedAtUnixMs { get; init; }

    /// <summary>
    /// The enum NAME, not its number.
    /// </summary>
    /// <remarks>
    /// A number would silently re-point at a different member if the enum ever gains
    /// one, and this is the field that decides whether a timestamp counts as consent.
    /// An unrecognised name reads as <c>NotRecorded</c>, which refuses.
    /// </remarks>
    [JsonPropertyName("provenance")]
    public string Provenance { get; init; } = string.Empty;

    [JsonPropertyName("disclosureVersion")]
    public string DisclosureVersion { get; init; } = string.Empty;

    [JsonPropertyName("blockedReason")]
    public string? BlockedReason { get; init; }

    [JsonPropertyName("preScanUnverified")]
    public bool PreScanUnverified { get; init; }

    [JsonPropertyName("updatedAtUnixMs")]
    public long UpdatedAtUnixMs { get; init; }
}
