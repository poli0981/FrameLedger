namespace FrameLedger.Domain.Detection;

/// <summary>How to recover a version string once a rule matched.</summary>
public sealed record VersionExtractor
{
    /// <summary>Which extraction to run.</summary>
    public required VersionExtractorType Type { get; init; }

    /// <summary>Regex source, for the regex extractors.</summary>
    public string? Value { get; init; }

    /// <summary>Sibling file to read, for <see cref="VersionExtractorType.PeFileVersion"/>.</summary>
    public string? From { get; init; }

    /// <summary>Dotted manifest path, for <see cref="VersionExtractorType.ManifestField"/>.</summary>
    public string? Field { get; init; }
}
