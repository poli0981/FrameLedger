using FluentAssertions;
using FrameLedger.Infrastructure.Blobs;

namespace FrameLedger.Infrastructure.Tests.Blobs;

/// <summary>
/// <c>14_TESTING</c>: "blob codecs round-trip for every series incl. two-pair render_res and three-bit
/// rt_flags, NaN forbidden".
/// </summary>
public sealed class SeriesCodecRoundTripTests
{
    [Fact]
    public void Float32RoundTripsAndCompresses()
    {
        float[] frametimes = [.. Enumerable.Range(0, 360_000).Select(i => 10f + (i % 7) * 0.5f)];

        byte[] blob = SeriesCodec.EncodeFloat32(frametimes);

        SeriesCodec.DecodeFloat32(blob).Should().Equal(frametimes);
        blob.Length.Should().BeLessThan(frametimes.Length * sizeof(float) / 2, "06_DATA_MODEL budgets an hour at 100 fps at <= 0.7 MB stored");
    }

    [Fact]
    public void NaNIsRefusedAtEncodeTime()
    {
        Action encode = () => SeriesCodec.EncodeFloat32([1f, float.NaN, 3f]);

        encode.Should().Throw<ArgumentException>().WithMessage("*index 1*");
    }

    [Fact]
    public void UInt16UInt32AndBytesRoundTrip()
    {
        ushort[] pso = [0, 1, 0, 40, ushort.MaxValue];
        uint[] rays = [0, 8_294_400, uint.MaxValue];
        byte[] rtFlags = [0b000, 0b001, 0b010, 0b100, 0b111];

        SeriesCodec.DecodeUInt16(SeriesCodec.EncodeUInt16(pso)).Should().Equal(pso);
        SeriesCodec.DecodeUInt32(SeriesCodec.EncodeUInt32(rays)).Should().Equal(rays);
        SeriesCodec.DecodeBytes(SeriesCodec.EncodeBytes(rtFlags)).Should().Equal(rtFlags, "all three bits preserved");
    }

    [Fact]
    public void EmptySeriesRoundTripToEmpty()
    {
        SeriesCodec.DecodeFloat32(SeriesCodec.EncodeFloat32([])).Should().BeEmpty();
        SeriesCodec.DecodeUInt32(SeriesCodec.EncodeUInt32([])).Should().BeEmpty();
    }

    [Fact]
    public void ATruncatedBlobIsRefusedRatherThanReadShort()
    {
        byte[] blob = SeriesCodec.EncodeUInt32([1, 2, 3]);
        // Re-deflate a 3-byte payload so the element boundary check is what fires, not the inflater.
        byte[] odd = SeriesCodec.EncodeBytes([1, 2, 3]);

        Action decode = () => SeriesCodec.DecodeUInt32(odd);

        decode.Should().Throw<InvalidDataException>();
        SeriesCodec.DecodeUInt32(blob).Should().Equal(1u, 2u, 3u);
    }

    [Fact]
    public void RenderResStoresTwoPairsPerFrame()
    {
        RenderRes[] frames = [new(1485, 835, 2560, 1440), new(1485, 835, 2560, 1440), new(1707, 960, 2560, 1440)];

        RenderRes[] back = RenderResCodec.Decode(RenderResCodec.Encode(frames));

        back.Should().Equal(frames);
        RenderResCodec.IsConstant(frames).Should().BeFalse();
        RenderResCodec.IsConstant(frames[..2]).Should().BeTrue("one tuple throughout, so the column may stay NULL");
        RenderResCodec.IsConstant([]).Should().BeTrue();
        SeriesCodec.DecodeUInt16(RenderResCodec.Encode(frames)).Should().HaveCount(12, "four uint16 per frame");
    }

    [Fact]
    public void ARenderResBlobWithAStrayPairIsRefused()
    {
        byte[] blob = SeriesCodec.EncodeUInt16([1485, 835, 2560]);

        Action decode = () => RenderResCodec.Decode(blob);

        decode.Should().Throw<InvalidDataException>();
    }

    [Fact]
    public void TheCodecTagIsTheOneTheSchemaStores() => SeriesCodec.Tag.Should().Be("deflate-le-v1");
}
