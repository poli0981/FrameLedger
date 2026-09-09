namespace FrameLedger.Application.Persistence;

/// <summary>
/// The blob encoding <c>06_DATA_MODEL</c> §Blob encoding specifies, as a port: the finalizer in
/// Application encodes every series through it, and <c>Infrastructure.Blobs</c> is the one implementation
/// (<c>deflate-le-v1</c>). NaN is refused at encode time; a truncated blob is refused at decode time.
/// </summary>
public interface ISeriesCodec
{
    /// <summary>What <c>frame_blobs.codec</c> / <c>sensor_blobs.codec</c> store.</summary>
    string Tag { get; }

    byte[] EncodeFloat32(ReadOnlySpan<float> values);

    byte[] EncodeUInt16(ReadOnlySpan<ushort> values);

    byte[] EncodeUInt32(ReadOnlySpan<uint> values);

    byte[] EncodeBytes(ReadOnlySpan<byte> values);
}
