namespace FrameLedger.Domain.Detection;

/// <summary>
/// What invalidates a cached detection result.
/// </summary>
/// <remarks>
/// <c>05_DETECTION</c> §Caching: re-run when the executable changes or the rules
/// do. Size and mtime together rather than a hash — hashing a multi-gigabyte
/// executable on every library render is not a trade this needs, and the pair
/// changes on any real update.
/// </remarks>
public readonly record struct DetectionCacheKey
{
    /// <summary>Full path to the executable.</summary>
    public required string ExePath { get; init; }

    /// <summary>Its size in bytes.</summary>
    public required long SizeBytes { get; init; }

    /// <summary>Its last-write time, unix milliseconds UTC.</summary>
    public required long MtimeUnixMs { get; init; }

    /// <summary>The rules version the cached result came from.</summary>
    public required string RulesVersion { get; init; }
}
