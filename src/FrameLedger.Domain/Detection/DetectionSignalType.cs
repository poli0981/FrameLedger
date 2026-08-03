namespace FrameLedger.Domain.Detection;

/// <summary>What a static signal looks for. Mirrors the schema's closed enum.</summary>
/// <remarks>
/// <see cref="PathContains"/> and the <c>strings_regex</c> version extractor are
/// in the shipped data and the schema but were missing from
/// <c>05_DETECTION</c>'s signal-type list — the schema said so in its own
/// <c>$comment</c> and nobody had made the edit.
/// </remarks>
public enum DetectionSignalType
{
    /// <summary>A file exists beneath the game directory.</summary>
    FileExists,

    /// <summary>A directory exists beneath the game directory.</summary>
    DirExists,

    /// <summary>A file beside the executable, matched as a glob.</summary>
    SiblingGlob,

    /// <summary>The install path contains a fragment, e.g. <c>steamapps\common</c>.</summary>
    PathContains,

    /// <summary>The executable's PE <c>CompanyName</c> contains a fragment.</summary>
    PeCompanyContains,

    /// <summary>The executable's PE <c>ProductName</c> contains a fragment.</summary>
    PeProductContains,

    /// <summary>A literal appears in the executable's bytes, within the 8 MB scan bound.</summary>
    StringsContains,

    /// <summary>A field in a store manifest. Not collected this phase.</summary>
    ManifestField,
}
