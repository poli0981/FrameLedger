namespace FrameLedger.Infrastructure.Persistence;

/// <summary>
/// Where the ledger lives. <c>01_ARCHITECTURE</c> §Data directory: <c>%LOCALAPPDATA%\FrameLedger\ledger.db</c>,
/// owned by the Agent alone (§S18 blocker 3).
/// </summary>
/// <remarks>
/// The DEFAULT is the only location production code names. A caller that opens a database elsewhere —
/// the unshipped capture host beside its own binary, a test in a scratch directory — passes the path
/// explicitly and says why; nothing here makes the location selectable by configuration.
/// </remarks>
public static class LedgerPaths
{
    public const string DatabaseFileName = "ledger.db";

    /// <summary><c>%LOCALAPPDATA%\FrameLedger</c>, asked of the shell rather than built from an inherited variable.</summary>
    public static string DefaultDirectory =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "FrameLedger");

    public static string DefaultDatabase => Path.Combine(DefaultDirectory, DatabaseFileName);
}
