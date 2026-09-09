using System.Text.Json.Serialization;

namespace FrameLedger.Application.Recording;

/// <summary>Source-generated JSON for the three JSON columns of <c>sessions</c>; no reflection.</summary>
[JsonSourceGenerationOptions(WriteIndented = false)]
[JsonSerializable(typeof(Dictionary<string, string?>))]
[JsonSerializable(typeof(Dictionary<string, int>))]
[JsonSerializable(typeof(NgxDriverWords))]
public sealed partial class RecordingJsonContext : JsonSerializerContext
{
}
