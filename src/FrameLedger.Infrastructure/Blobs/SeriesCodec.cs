using System.Buffers.Binary;
using System.IO.Compression;

namespace FrameLedger.Infrastructure.Blobs;

/// <summary>
/// <c>06_DATA_MODEL</c> §Blob encoding: little-endian arrays through <see cref="DeflateStream"/> (Optimal).
/// One codec tag names the whole scheme so a future change is a second tag, never a silent re-read.
/// </summary>
/// <remarks>
/// <b>NaN is forbidden and asserted.</b> A NaN frame time would survive the round trip and poison every
/// percentile downstream; the calculators never produce one, so one arriving here is a defect, and
/// <see cref="EncodeFloat32"/> throws rather than storing it.
/// </remarks>
public static class SeriesCodec
{
    /// <summary>The scheme every blob written by this build carries in <c>codec</c>.</summary>
    public const string Tag = "deflate-le-v1";

    public static byte[] EncodeFloat32(IReadOnlyList<float> values)
    {
        ArgumentNullException.ThrowIfNull(values);

        byte[] raw = new byte[values.Count * sizeof(float)];
        for (int i = 0; i < values.Count; i++)
        {
            if (float.IsNaN(values[i]))
            {
                throw new ArgumentException($"NaN at index {i}: a series may not carry one (06_DATA_MODEL §Blob encoding)", nameof(values));
            }

            BinaryPrimitives.WriteSingleLittleEndian(raw.AsSpan(i * sizeof(float)), values[i]);
        }

        return Deflate(raw);
    }

    public static float[] DecodeFloat32(byte[] blob)
    {
        byte[] raw = Inflate(blob, sizeof(float));
        var values = new float[raw.Length / sizeof(float)];
        for (int i = 0; i < values.Length; i++)
        {
            values[i] = BinaryPrimitives.ReadSingleLittleEndian(raw.AsSpan(i * sizeof(float)));
        }

        return values;
    }

    public static byte[] EncodeUInt16(IReadOnlyList<ushort> values)
    {
        ArgumentNullException.ThrowIfNull(values);

        byte[] raw = new byte[values.Count * sizeof(ushort)];
        for (int i = 0; i < values.Count; i++)
        {
            BinaryPrimitives.WriteUInt16LittleEndian(raw.AsSpan(i * sizeof(ushort)), values[i]);
        }

        return Deflate(raw);
    }

    public static ushort[] DecodeUInt16(byte[] blob)
    {
        byte[] raw = Inflate(blob, sizeof(ushort));
        var values = new ushort[raw.Length / sizeof(ushort)];
        for (int i = 0; i < values.Length; i++)
        {
            values[i] = BinaryPrimitives.ReadUInt16LittleEndian(raw.AsSpan(i * sizeof(ushort)));
        }

        return values;
    }

    public static byte[] EncodeUInt32(IReadOnlyList<uint> values)
    {
        ArgumentNullException.ThrowIfNull(values);

        byte[] raw = new byte[values.Count * sizeof(uint)];
        for (int i = 0; i < values.Count; i++)
        {
            BinaryPrimitives.WriteUInt32LittleEndian(raw.AsSpan(i * sizeof(uint)), values[i]);
        }

        return Deflate(raw);
    }

    public static uint[] DecodeUInt32(byte[] blob)
    {
        byte[] raw = Inflate(blob, sizeof(uint));
        var values = new uint[raw.Length / sizeof(uint)];
        for (int i = 0; i < values.Length; i++)
        {
            values[i] = BinaryPrimitives.ReadUInt32LittleEndian(raw.AsSpan(i * sizeof(uint)));
        }

        return values;
    }

    /// <summary>Per-frame bytes — flags, RT evidence — stored verbatim, all bits preserved.</summary>
    public static byte[] EncodeBytes(IReadOnlyList<byte> values)
    {
        ArgumentNullException.ThrowIfNull(values);
        return Deflate([.. values]);
    }

    public static byte[] DecodeBytes(byte[] blob) => Inflate(blob, 1);

    private static byte[] Deflate(byte[] raw)
    {
        using var output = new MemoryStream();
        using (var deflate = new DeflateStream(output, CompressionLevel.Optimal, leaveOpen: true))
        {
            deflate.Write(raw);
        }

        return output.ToArray();
    }

    private static byte[] Inflate(byte[] blob, int elementSize)
    {
        ArgumentNullException.ThrowIfNull(blob);

        using var input = new MemoryStream(blob);
        using var inflate = new DeflateStream(input, CompressionMode.Decompress);
        using var output = new MemoryStream();
        inflate.CopyTo(output);
        byte[] raw = output.ToArray();
        if (raw.Length % elementSize != 0)
        {
            throw new InvalidDataException($"a {Tag} blob of {raw.Length} byte(s) is not a whole number of {elementSize}-byte elements");
        }

        return raw;
    }
}
