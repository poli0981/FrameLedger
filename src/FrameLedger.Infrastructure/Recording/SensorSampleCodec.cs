using System.Buffers.Binary;
using FrameLedger.Application.Telemetry;

namespace FrameLedger.Infrastructure.Recording;

/// <summary>
/// One <see cref="TelemetrySample"/> as bytes, for the <c>.partial</c>: QPC, wall clock, layer, then a
/// presence mask and only the fields that carry a value — null stays null across the round trip, which
/// is the whole point (N/A is never 0).
/// </summary>
internal static class SensorSampleCodec
{
    private const int _fieldCount = 12;
    private const int _fixedBytes = 8 + 8 + 1 + 2;

    public static int SizeOf(in TelemetrySample sample) => _fixedBytes + 8 * PresentCount(sample.Sample);

    public static int Write(Span<byte> into, in TelemetrySample sample)
    {
        GpuSample s = sample.Sample;
        BinaryPrimitives.WriteInt64LittleEndian(into, sample.QpcTicks);
        BinaryPrimitives.WriteInt64LittleEndian(into[8..], s.TakenAt.ToUnixTimeMilliseconds());
        into[16] = (byte)s.Layer;
        ushort mask = 0;
        int at = _fixedBytes;
        double?[] fields = Fields(s);
        for (int i = 0; i < _fieldCount; i++)
        {
            if (fields[i] is { } v)
            {
                mask |= (ushort)(1 << i);
                BinaryPrimitives.WriteDoubleLittleEndian(into[at..], v);
                at += 8;
            }
        }

        BinaryPrimitives.WriteUInt16LittleEndian(into[17..], mask);
        return at;
    }

    /// <summary>Reads one sample; returns the bytes consumed, or 0 when the span is too short.</summary>
    public static int Read(ReadOnlySpan<byte> from, out TelemetrySample sample)
    {
        sample = default;
        if (from.Length < _fixedBytes)
        {
            return 0;
        }

        long qpc = BinaryPrimitives.ReadInt64LittleEndian(from);
        long takenAt = BinaryPrimitives.ReadInt64LittleEndian(from[8..]);
        var layer = (TelemetryLayer)from[16];
        ushort mask = BinaryPrimitives.ReadUInt16LittleEndian(from[17..]);
        int present = System.Numerics.BitOperations.PopCount(mask);
        if (from.Length < _fixedBytes + 8 * present)
        {
            return 0;
        }

        var fields = new double?[_fieldCount];
        int at = _fixedBytes;
        for (int i = 0; i < _fieldCount; i++)
        {
            if ((mask & (1 << i)) != 0)
            {
                fields[i] = BinaryPrimitives.ReadDoubleLittleEndian(from[at..]);
                at += 8;
            }
        }

        sample = new TelemetrySample(qpc, new GpuSample
        {
            TakenAt = DateTimeOffset.FromUnixTimeMilliseconds(takenAt),
            Layer = layer,
            TempCoreC = fields[0],
            TempHotspotC = fields[1],
            TempMemoryC = fields[2],
            LoadPct = fields[3],
            VramAdapterMb = fields[4],
            CoreClockMhz = fields[5],
            MemClockMhz = fields[6],
            PowerW = fields[7],
            FanRpm = fields[8],
            ThrottleReasons = fields[9] is { } t ? (uint)t : null,
            PcieGen = fields[10] is { } g ? (int)g : null,
            PcieWidth = fields[11] is { } w ? (int)w : null,
        });
        return at;
    }

    private static int PresentCount(GpuSample s) => Fields(s).Count(static f => f is not null);

    private static double?[] Fields(GpuSample s) =>
    [
        s.TempCoreC, s.TempHotspotC, s.TempMemoryC, s.LoadPct, s.VramAdapterMb, s.CoreClockMhz, s.MemClockMhz,
        s.PowerW, s.FanRpm, s.ThrottleReasons, s.PcieGen, s.PcieWidth,
    ];
}
