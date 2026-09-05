namespace FrameLedger.CaptureHost.Consume;

/// <summary>
/// The census-named modules a session's target had loaded, with versions, merged over every
/// snapshot the loop took.
/// </summary>
/// <remarks>
/// <para>
/// <b>Why this exists.</b> <c>FlWriterState.runtimeCensus</c> says a module of a NAME was loaded and
/// nothing else. Dying Light: The Beast and Cyberpunk 2077 both load a Streamline interposer; one
/// ships 2.8.0 and the other 2.7.1, and <c>20_OPEN_QUESTIONS</c> §H5 case 3 turns on exactly that
/// difference. The census cannot carry it without a layout change; this can, from outside the
/// process, at no cost on the present path.
/// </para>
/// <para>
/// <b>First seen wins per name.</b> A module that unloads and reloads between snapshots keeps its
/// first version; the report is about what the session ran, not about the last thing the loader
/// said. <see cref="Snapshots"/> and <see cref="Unreadable"/> are counted rather than folded into
/// one flag, because a target that exits before the final snapshot makes that snapshot fail
/// without making the earlier ones wrong.
/// </para>
/// </remarks>
internal sealed record RuntimeModuleSet(IReadOnlyList<RuntimeModuleInfo> Modules, int Snapshots, int Unreadable)
{
    public static RuntimeModuleSet Empty { get; } = new([], 0, 0);

    /// <summary>
    /// The parsed file version of <paramref name="fileName"/>, or null when the module was never
    /// seen or carries no version resource.
    /// </summary>
    public Version? VersionOf(string fileName) => Find(fileName)?.Parsed;

    public RuntimeModuleInfo? Find(string fileName) =>
        Modules.FirstOrDefault(m => string.Equals(m.FileName, fileName, StringComparison.OrdinalIgnoreCase));

    /// <summary>This set, then <paramref name="later"/>'s modules that this one did not already name.</summary>
    public RuntimeModuleSet Merge(RuntimeModuleSet later)
    {
        ArgumentNullException.ThrowIfNull(later);
        var merged = new List<RuntimeModuleInfo>(Modules);
        foreach (RuntimeModuleInfo m in later.Modules)
        {
            if (!merged.Exists(x => string.Equals(x.FileName, m.FileName, StringComparison.OrdinalIgnoreCase)))
            {
                merged.Add(m);
            }
        }

        return new RuntimeModuleSet(merged, Snapshots + later.Snapshots, Unreadable + later.Unreadable);
    }
}
