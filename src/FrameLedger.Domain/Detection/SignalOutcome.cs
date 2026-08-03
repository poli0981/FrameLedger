namespace FrameLedger.Domain.Detection;

/// <summary>
/// The answer to one signal — deliberately three-valued.
/// </summary>
/// <remarks>
/// <para>
/// <see cref="Unknown"/> is 0 so a default value has established nothing. The
/// two-valued version of this type is the defect that produced this project's
/// worst finding: an evidence source that could not be read reported the same
/// value as one that was read and came back clean.
/// </para>
/// <para>
/// The stakes are lower here than in the guard — a wrong inference mislabels a
/// library card, it does not cost somebody an account — but the shape is the
/// same and so is the fix.
/// </para>
/// </remarks>
public enum SignalOutcome
{
    /// <summary>Could not be established. Never conflate with <see cref="NoMatch"/>.</summary>
    Unknown = 0,

    /// <summary>Looked, and found it.</summary>
    Match,

    /// <summary>Looked, and it is not there.</summary>
    NoMatch,
}
