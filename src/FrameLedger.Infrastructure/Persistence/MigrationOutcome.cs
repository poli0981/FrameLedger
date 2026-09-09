namespace FrameLedger.Infrastructure.Persistence;

/// <summary>What opening the ledger did to its schema. Every value is reportable to a human.</summary>
public enum MigrationOutcome
{
    /// <summary>The schema was already at this build's version; nothing ran.</summary>
    AlreadyCurrent = 0,

    /// <summary>One or more scripts ran and the schema is now at this build's version.</summary>
    Applied = 1,

    /// <summary>
    /// The database carries a version NEWER than any script this build knows. Refused: reading a schema
    /// we do not understand is the one option that could turn a newer ledger into wrong answers.
    /// </summary>
    NewerThanThisBuild = 2,
}
