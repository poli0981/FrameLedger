using System.Runtime.InteropServices;
using FrameLedger.Shared;

namespace FrameLedger.Application.Recording;

/// <summary>The drain's accounting at one flush, and the writer's state word; the last one written wins.</summary>
[StructLayout(LayoutKind.Auto)]
public readonly record struct PartialTick(
    long DrainTicks,
    long ForegroundTicks,
    long TotalDropped,
    long TotalGaps,
    uint GuardTicksPublished,
    long WrittenAtUnixMs,
    FlWriterState WriterState);
