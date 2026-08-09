using System.Text.Json.Serialization;

namespace FrameLedger.CaptureHost.Consent;

/// <summary>
/// The on-disk shape. Deliberately dumb: it carries no behaviour and no defaults
/// that mean anything, because everything that decides is on
/// <c>GameConsentRecord</c>.
/// </summary>
/// <remarks>
/// <b>This is not a migration source for P2's SQLite.</b> <c>06_DATA_MODEL</c>
/// declines to write <c>0001_init.sql</c> before its consumers exist, on the
/// ground that guessing a shape bakes in a wrong guess that can only be appended
/// to — and a file format invented now would become exactly the artifact that
/// schema had to accommodate. It lives in the unshipped host's own build output
/// and dies with the build tree.
/// </remarks>
internal sealed record ConsentFile
{
    /// <summary>Bumped if the shape changes. An unknown version reads as no records at all.</summary>
    [JsonPropertyName("version")]
    public int Version { get; init; }

    [JsonPropertyName("games")]
    public IReadOnlyList<ConsentFileEntry> Games { get; init; } = [];
}
