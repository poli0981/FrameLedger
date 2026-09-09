namespace FrameLedger.Application.Persistence;

/// <summary>
/// The <c>frame_blobs</c> row, already encoded (<c>Infrastructure.Blobs.SeriesCodec</c>). <c>frametimes</c> and <c>frame_flags</c> are mandatory;
/// the rest are null when nothing measured them.
/// </summary>
public sealed record FrameBlobs
{
    public required string Codec { get; init; }

    public required long SampleCount { get; init; }

    public required ReadOnlyMemory<byte> FrameTimes { get; init; }

    public required ReadOnlyMemory<byte> FrameFlags { get; init; }

    public ReadOnlyMemory<byte>? FrameIndex { get; init; }

    public ReadOnlyMemory<byte>? SwapchainIds { get; init; }

    public ReadOnlyMemory<byte>? RtFlags { get; init; }

    public ReadOnlyMemory<byte>? RenderRes { get; init; }

    public ReadOnlyMemory<byte>? DispatchRays { get; init; }

    public ReadOnlyMemory<byte>? PsoCreated { get; init; }

    public ReadOnlyMemory<byte>? VramProc { get; init; }

    public ReadOnlyMemory<byte>? LatencyUs { get; init; }
}
