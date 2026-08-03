namespace FrameLedger.Domain.Detection;

/// <summary>How an engine's version is recovered once its rule matched.</summary>
public enum VersionExtractorType
{
    /// <summary>PE <c>FileVersion</c> of a named sibling file.</summary>
    PeFileVersion,

    /// <summary>First capture group of a regex over the executable's PE <c>ProductVersion</c>.</summary>
    PeProductVersionRegex,

    /// <summary>First capture group of a regex over the bounded strings scan.</summary>
    StringsRegex,

    /// <summary>A field in a store manifest. Not collected this phase.</summary>
    ManifestField,
}
