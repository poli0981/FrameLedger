using System.Runtime.InteropServices;

namespace FrameLedger.Application.Telemetry;

/// <summary>
/// One reading the poller took, stamped with the clock the ring's records carry.
/// </summary>
/// <remarks>
/// <para>
/// <see cref="QpcTicks"/> is the raw <c>QueryPerformanceCounter</c> value at the moment the
/// poller read the sample, the same clock <c>FlFrameRecord.qpcTicks</c> is in, so a sensor
/// series can be laid beside a frame series without a second clock domain. It stays raw:
/// the recorder converts through the session's <c>qpc_epoch</c> / <c>qpc_frequency</c> pair,
/// and QPC never leaves the pipeline any other way (<c>06_DATA_MODEL</c>).
/// </para>
/// <para>
/// <see cref="Sample"/> keeps its own <see cref="GpuSample.TakenAt"/> (wall clock, from the
/// layer that produced it). The two are different instants — the poller reads what the layer
/// last published — and the gap between them is the layer's own cadence, at most one interval.
/// </para>
/// </remarks>
[StructLayout(LayoutKind.Auto)]
public readonly record struct TelemetrySample(long QpcTicks, GpuSample Sample);
