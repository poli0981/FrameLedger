namespace FrameLedger.Application.Rules;

/// <summary>What <see cref="RulesSeeder"/> did, and why.</summary>
/// <remarks>
/// Every value is reportable. §S20's failure mode is silence: a seeder that wrote
/// nothing and said nothing leaves the guard answering <c>RulesUnreadable</c> for
/// every title with no trace of why.
/// </remarks>
public enum RulesSeedOutcome
{
    /// <summary>Nothing was there; the packaged seed was installed.</summary>
    Installed = 0,

    /// <summary>
    /// The installed file was one we had installed, and this build ships a
    /// different one. Replaced.
    /// </summary>
    Updated,

    /// <summary>The installed file is already exactly the packaged seed.</summary>
    AlreadyCurrent,

    /// <summary>
    /// A usable file is there that we did not install. Left alone.
    /// </summary>
    /// <remarks>
    /// It cannot weaken the gate: since the floor is generated from the shipped
    /// blocklist, a rules file can only ADD (`19_SAFETY` §The floor data cannot
    /// remove). So the safe act is to leave it and say so, rather than destroy
    /// something a user or a support process put there.
    /// </remarks>
    ForeignLeftAlone,

    /// <summary>
    /// The installed file is not usable by the guard, so it was replaced.
    /// </summary>
    /// <remarks>
    /// There is nothing to clobber: the guard already refuses every title on that
    /// file. Leaving it would make a corrupt or truncated file a permanent,
    /// unrepairable machine-wide refusal, because nothing else in the product
    /// repairs it.
    /// </remarks>
    ReplacedUnusable,

    /// <summary>
    /// Somebody else won the race to create the file between our look and our
    /// write. Not an error.
    /// </summary>
    RaceLost,

    /// <summary>
    /// The seed shipped in this build is not usable by the guard. Nothing was
    /// installed.
    /// </summary>
    /// <remarks>
    /// Should be unreachable: <c>rules-validate.ps1</c> and ctest
    /// <c>fl_rules_budget</c> both gate the packaged seed, and the latter asserts
    /// that exact file parses in the guard. Checked anyway, because "our own
    /// artifact is fine" is the assumption this project keeps finding to be false.
    /// </remarks>
    PackagedSeedUnusable,

    /// <summary>The rules location could not be written. Reported, not swallowed.</summary>
    WriteFailed,
}
