using System.Text.Json;
using FrameLedger.Application.Detection;
using FrameLedger.Domain.Detection;

namespace FrameLedger.Infrastructure.Detection;

/// <summary>
/// Reads the detection half of <c>detection-rules.json</c>.
/// </summary>
/// <remarks>
/// Every failure is an exception rather than a partial rule set. A detector that
/// silently drops half its rules reports "no engine detected" across a whole
/// library, which looks exactly like working software.
/// </remarks>
public sealed class DetectionRulesFile : IDetectionRulesSource
{
    private readonly string _path;

    /// <summary>Reads from <see cref="DefaultPath"/>.</summary>
    public DetectionRulesFile() : this(DefaultPath)
    {
    }

    /// <summary>Reads from an explicit path. Tests and the fixture corpus only.</summary>
    /// <remarks>
    /// Not reachable through <see cref="IDetectionRulesSource"/>, which takes no
    /// path — the port is what the rest of the application sees, and it offers
    /// no way to redirect the source (§S3's shape).
    /// </remarks>
    public DetectionRulesFile(string path) =>
        _path = path ?? throw new ArgumentNullException(nameof(path));

    /// <summary>The one location the product reads rules from.</summary>
    /// <remarks>
    /// The same directory the native guard uses (<c>fl_ac_rules.h</c>), reached
    /// independently rather than through the ABI: this side reads only
    /// engines/platforms/capabilities, and exporting a path from the guard would
    /// be a wider surface than the need.
    /// </remarks>
    public static string DefaultPath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "FrameLedger", "rules", "detection-rules.json");

    /// <inheritdoc />
    public async ValueTask<DetectionRuleSet> LoadAsync(CancellationToken ct = default)
    {
        string json;
        try
        {
            json = await File.ReadAllTextAsync(_path, ct).ConfigureAwait(false);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            throw new InvalidOperationException($"detection rules could not be read from '{_path}'", ex);
        }

        DetectionRulesDto.Rules? dto;
        try
        {
            dto = JsonSerializer.Deserialize(json, DetectionRulesJsonContext.Default.Rules);
        }
        catch (JsonException ex)
        {
            throw new InvalidOperationException($"detection rules at '{_path}' are not the shape we require", ex);
        }

        if (dto is null)
        {
            throw new InvalidOperationException($"detection rules at '{_path}' are empty");
        }

        // A const in the schema, and refused rather than coerced here for the
        // same reason: a file declaring another version describes a shape we do
        // not know how to read, and guessing is how a rule silently stops firing.
        if (dto.SchemaVersion != 2)
        {
            throw new InvalidOperationException(
                $"detection rules declare schemaVersion {dto.SchemaVersion}; this build reads 2");
        }

        return new DetectionRuleSet
        {
            SchemaVersion = dto.SchemaVersion,
            RulesVersion = Required(dto.RulesVersion, "rulesVersion"),
            Engines = [.. (dto.Engines ?? []).Select(ToEngine)],
            Platforms = [.. (dto.Platforms ?? []).Select(ToPlatform)],
            Capabilities = [.. (dto.Capabilities ?? []).Select(ToCapability)],
        };
    }

    private static EngineRule ToEngine(DetectionRulesDto.Engine d)
    {
        string id = Required(d.Id, "engine id");
        return new EngineRule
        {
            Id = id,
            Name = Required(d.Name, $"name of engine '{id}'"),
            Signals = ToGroup(d.Signals, id),

            // Explicit null means "no version extractor". An absent key never
            // reaches here — [JsonRequired] rejects the document first.
            Version = d.Version is null ? null : ToExtractor(d.Version, id),
        };
    }

    private static PlatformRule ToPlatform(DetectionRulesDto.Platform d)
    {
        string id = Required(d.Id, "platform id");
        return new PlatformRule
        {
            Id = id,
            Name = Required(d.Name, $"name of platform '{id}'"),
            Signals = ToGroup(d.Signals, id),
        };
    }

    /// <summary>Normalises a capability's flat glob list into an <c>any</c> group.</summary>
    /// <remarks>
    /// The schema keeps capabilities flat and calls unifying the shapes a
    /// schemaVersion-3 change. Normalising here means the evaluator sees one
    /// shape while the difference stays at the data boundary, where it belongs.
    /// </remarks>
    private static CapabilityRule ToCapability(DetectionRulesDto.Capability d)
    {
        string id = Required(d.Id, "capability id");
        if (d.Signals is null || d.Signals.Count == 0)
        {
            throw new InvalidOperationException($"capability '{id}' has no signals");
        }

        return new CapabilityRule
        {
            Id = id,
            Name = Required(d.Name, $"name of capability '{id}'"),
            Signals = new SignalGroup
            {
                Combinator = SignalCombinator.Any,
                Signals = [.. d.Signals.Select(g => new DetectionSignal
                {
                    Type = DetectionSignalType.SiblingGlob,
                    Value = g,
                })],
            },
        };
    }

    private static SignalGroup ToGroup(DetectionRulesDto.Group? d, string owner)
    {
        if (d is null)
        {
            throw new InvalidOperationException($"'{owner}' has no signals");
        }

        // maxProperties: 1 in the schema. Both-at-once is rejected rather than
        // given an invented precedence.
        if (d.All is not null && d.Any is not null)
        {
            throw new InvalidOperationException($"'{owner}' declares both `all` and `any`");
        }

        List<DetectionRulesDto.Signal>? raw = d.All ?? d.Any;
        if (raw is null || raw.Count == 0)
        {
            throw new InvalidOperationException($"'{owner}' has an empty signal group");
        }

        return new SignalGroup
        {
            Combinator = d.All is not null ? SignalCombinator.All : SignalCombinator.Any,
            Signals = [.. raw.Select(s => ToSignal(s, owner))],
        };
    }

    private static DetectionSignal ToSignal(DetectionRulesDto.Signal d, string owner)
    {
        DetectionSignalType type = d.Type switch
        {
            "file_exists" => DetectionSignalType.FileExists,
            "dir_exists" => DetectionSignalType.DirExists,
            "sibling_glob" => DetectionSignalType.SiblingGlob,
            "path_contains" => DetectionSignalType.PathContains,
            "pe_company_contains" => DetectionSignalType.PeCompanyContains,
            "pe_product_contains" => DetectionSignalType.PeProductContains,
            "strings_contains" => DetectionSignalType.StringsContains,
            "manifest_field" => DetectionSignalType.ManifestField,
            _ => throw new InvalidOperationException($"'{owner}' has unknown signal type '{d.Type}'"),
        };

        // `field` belongs only on manifest_field, and is required there. The
        // schema expresses this as a `false` schema rather than ignoring the
        // key, because an ignored key is a rule that does not do what it says.
        bool isManifest = type == DetectionSignalType.ManifestField;
        if (isManifest && string.IsNullOrEmpty(d.Field))
        {
            throw new InvalidOperationException($"'{owner}' has a manifest_field signal with no `field`");
        }

        if (!isManifest && d.Field is not null)
        {
            throw new InvalidOperationException($"'{owner}' has `field` on a {d.Type} signal");
        }

        return new DetectionSignal
        {
            Type = type,
            Value = Required(d.Value, $"signal value in '{owner}'"),
            Field = d.Field,
        };
    }

    private static VersionExtractor ToExtractor(DetectionRulesDto.Extractor d, string owner) => new()
    {
        Type = d.Type switch
        {
            "pe_file_version" => VersionExtractorType.PeFileVersion,
            "pe_product_version_regex" => VersionExtractorType.PeProductVersionRegex,
            "strings_regex" => VersionExtractorType.StringsRegex,
            "manifest_field" => VersionExtractorType.ManifestField,
            _ => throw new InvalidOperationException($"'{owner}' has unknown version extractor '{d.Type}'"),
        },
        Value = d.Value,
        From = d.From,
        Field = d.Field,
    };

    private static string Required(string? value, string what) =>
        string.IsNullOrWhiteSpace(value)
            ? throw new InvalidOperationException($"{what} is missing")
            : value;
}
