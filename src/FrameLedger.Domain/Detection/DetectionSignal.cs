namespace FrameLedger.Domain.Detection;

/// <summary>One thing to look for on disk.</summary>
public sealed record DetectionSignal
{
    /// <summary>What kind of look-up this is.</summary>
    public required DetectionSignalType Type { get; init; }

    /// <summary>The needle. May contain <c>*</c> and <c>${ExeName}</c>.</summary>
    public required string Value { get; init; }

    /// <summary>
    /// Dotted path into a store manifest. Non-null only for
    /// <see cref="DetectionSignalType.ManifestField"/> — the schema makes the
    /// key a <c>false</c> schema on every other type rather than merely ignoring
    /// it, and the reader enforces the same.
    /// </summary>
    public string? Field { get; init; }
}
