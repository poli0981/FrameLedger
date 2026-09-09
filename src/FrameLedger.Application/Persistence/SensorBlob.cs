namespace FrameLedger.Application.Persistence;

/// <summary>One <c>sensor_blobs</c> row.</summary>
public sealed record SensorBlob
{
    public required string Series { get; init; }

    public required double Hz { get; init; }

    public required string Codec { get; init; }

    public required ReadOnlyMemory<byte> Data { get; init; }
}
