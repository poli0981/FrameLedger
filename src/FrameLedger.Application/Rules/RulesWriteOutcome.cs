namespace FrameLedger.Application.Rules;

/// <summary>What the store's write attempt did.</summary>
public enum RulesWriteOutcome
{
    /// <summary>The file is now the content we asked for.</summary>
    Written = 0,

    /// <summary>
    /// A file appeared between the look and the write, and we were told not to
    /// replace it. Not an error — somebody else seeded it.
    /// </summary>
    AlreadyExists,

    /// <summary>Could not write. The caller reports it rather than retrying blindly.</summary>
    Failed,
}
