using System.Runtime.InteropServices;

namespace FrameLedger.Shared;

/// <summary>
/// What one drain call saw. Counts are per-call; <c>ShmRingReader</c> accumulates totals. Lives beside
/// the record it counts (P2 PR-C): the session loop in Application reads it through <c>ICaptureSink</c>
/// and Application does not reference Infrastructure.
/// </summary>
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
/// <param name="FirstSlot">
/// The ring index of the first slot this call examined, AFTER any overwrite skip. With it and the gap
/// indices a caller can say which copied record a torn slot preceded — the position <c>07_IPC</c> wants
/// a gap recorded at — rather than only how many there were (P2 PR-D).
/// </param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct DrainResult(int Copied, int Gaps, long Dropped, ulong FirstSlot = 0);
