using System.Runtime.InteropServices;
using FrameLedger.Application.Persistence;

namespace FrameLedger.Application.Tests.Recording;

/// <summary>An <see cref="ISeriesCodec"/> that stores the little-endian bytes uncompressed, so a test can read them back by hand.</summary>
internal sealed class RawSeriesCodec : ISeriesCodec
{
    public string Tag => "raw-le-test";

    public byte[] EncodeFloat32(ReadOnlySpan<float> values) => MemoryMarshal.AsBytes(values).ToArray();

    public byte[] EncodeUInt16(ReadOnlySpan<ushort> values) => MemoryMarshal.AsBytes(values).ToArray();

    public byte[] EncodeUInt32(ReadOnlySpan<uint> values) => MemoryMarshal.AsBytes(values).ToArray();

    public byte[] EncodeBytes(ReadOnlySpan<byte> values) => values.ToArray();

    public static float[] Floats(ReadOnlyMemory<byte> blob) => MemoryMarshal.Cast<byte, float>(blob.Span).ToArray();

    public static uint[] UInt32s(ReadOnlyMemory<byte> blob) => MemoryMarshal.Cast<byte, uint>(blob.Span).ToArray();

    public static ushort[] UInt16s(ReadOnlyMemory<byte> blob) => MemoryMarshal.Cast<byte, ushort>(blob.Span).ToArray();
}
