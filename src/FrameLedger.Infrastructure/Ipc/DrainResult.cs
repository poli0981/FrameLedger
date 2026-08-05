using System.Runtime.InteropServices;

namespace FrameLedger.Infrastructure.Ipc;

/// <summary>What one drain call saw. Counts are per-call; <see cref="ShmRingReader"/> accumulates totals.</summary>
/// <param name="Copied">Records accepted.</param>
/// <param name="Gaps">
/// Torn slots. A DATA GAP, never a skipped frame: dropping one silently merges the two surrounding frame
/// times into one double-length interval — it fabricates a stutter in the metric this product exists to
/// report honestly (<c>07_IPC</c> §Protocol rules, <c>03_METRICS</c>).
/// </param>
/// <param name="Dropped">
/// Records overwritten before we consumed them. Computed by the reader, which owns the read index; the
/// writer has none and cannot know whether the slot it overwrites was ever taken.
/// </param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct DrainResult(int Copied, int Gaps, long Dropped);
