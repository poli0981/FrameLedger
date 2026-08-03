using System.Text.Json.Serialization;

namespace FrameLedger.Infrastructure.Detection;

/// <summary>
/// The wire shape of <c>detection-rules.json</c>, detection half only.
/// </summary>
/// <remarks>
/// <para>
/// <strong>There is no <c>anticheat</c> member and there never will be.</strong>
/// <c>System.Text.Json</c> ignores unknown members, so the blocklist is simply
/// not deserialised: there is exactly one matcher and it is native (§S15 item 1).
/// A managed type that merely <em>held</em> that data would be one refactor away
/// from being a second one, and <c>NoSecondMatcherTests</c> asserts it stays
/// absent.
/// </para>
/// <para>
/// Nested inside one container so the file carries a single top-level type;
/// they are an implementation detail of <see cref="DetectionRulesFile"/> and
/// nothing outside it should name them.
/// </para>
/// </remarks>
internal static class DetectionRulesDto
{
    internal sealed class Rules
    {
        [JsonPropertyName("schemaVersion")] public int SchemaVersion { get; set; }

        [JsonPropertyName("rulesVersion")] public string? RulesVersion { get; set; }

        [JsonPropertyName("engines")] public List<Engine>? Engines { get; set; }

        [JsonPropertyName("platforms")] public List<Platform>? Platforms { get; set; }

        [JsonPropertyName("capabilities")] public List<Capability>? Capabilities { get; set; }
    }

    internal sealed class Engine
    {
        [JsonPropertyName("id")] public string? Id { get; set; }

        [JsonPropertyName("name")] public string? Name { get; set; }

        [JsonPropertyName("signals")] public Group? Signals { get; set; }

        /// <summary>
        /// Required by the schema, nullable in value.
        /// </summary>
        /// <remarks>
        /// <c>[JsonRequired]</c> on a nullable property is what separates "the
        /// key is absent" — a malformed file — from "the key is explicitly
        /// null", which states that this engine has no version extractor. The
        /// schema makes it required-and-nullable precisely so the two cannot be
        /// confused; losing that here would turn a broken rule into a silent one.
        /// </remarks>
        [JsonRequired]
        [JsonPropertyName("version")]
        public Extractor? Version { get; set; }
    }

    internal sealed class Platform
    {
        [JsonPropertyName("id")] public string? Id { get; set; }

        [JsonPropertyName("name")] public string? Name { get; set; }

        [JsonPropertyName("signals")] public Group? Signals { get; set; }
    }

    internal sealed class Capability
    {
        [JsonPropertyName("id")] public string? Id { get; set; }

        [JsonPropertyName("name")] public string? Name { get; set; }

        /// <summary>A flat glob list — a different shape from engines and platforms, per the schema.</summary>
        [JsonPropertyName("signals")] public List<string>? Signals { get; set; }
    }

    internal sealed class Group
    {
        [JsonPropertyName("all")] public List<Signal>? All { get; set; }

        [JsonPropertyName("any")] public List<Signal>? Any { get; set; }
    }

    internal sealed class Signal
    {
        [JsonPropertyName("type")] public string? Type { get; set; }

        [JsonPropertyName("value")] public string? Value { get; set; }

        [JsonPropertyName("field")] public string? Field { get; set; }
    }

    internal sealed class Extractor
    {
        [JsonPropertyName("type")] public string? Type { get; set; }

        [JsonPropertyName("value")] public string? Value { get; set; }

        [JsonPropertyName("from")] public string? From { get; set; }

        [JsonPropertyName("field")] public string? Field { get; set; }
    }
}
