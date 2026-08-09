using System.Text.Json.Serialization;

namespace FrameLedger.CaptureHost.Consent;

/// <summary>Source-generated serialisation, matching the repo's other JSON contexts.</summary>
[JsonSourceGenerationOptions(WriteIndented = true)]
[JsonSerializable(typeof(ConsentFile))]
internal sealed partial class ConsentFileJsonContext : JsonSerializerContext;
