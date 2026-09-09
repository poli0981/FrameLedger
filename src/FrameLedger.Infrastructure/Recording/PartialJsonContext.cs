using System.Text.Json.Serialization;
using FrameLedger.Application.Recording;

namespace FrameLedger.Infrastructure.Recording;

/// <summary>Source-generated JSON for the <c>.partial</c> header — no reflection, enums by name so the file reads as it is.</summary>
[JsonSourceGenerationOptions(UseStringEnumConverter = true, WriteIndented = false)]
[JsonSerializable(typeof(PartialHeader))]
internal sealed partial class PartialJsonContext : JsonSerializerContext
{
}
