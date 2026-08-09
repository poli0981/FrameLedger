using FluentAssertions;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>
/// The unmeasured-prefix case, which the segmenter got wrong in exactly the way its
/// own comment forbids.
/// </summary>
/// <remarks>
/// <c>w</c>/<c>h</c> start at 0 and were used both as "no baseline yet" and as a real
/// resolution, so the split test fired on the FIRST measured record of a stream
/// whenever unmeasured records preceded it — emitting a leading segment reported as
/// 0×0, which is cutting a segment on the writer's silence.
/// </remarks>
public sealed class SegmenterBaselineTests
{
    private static FlFrameRecord Measured(uint index, ushort w, ushort h) => new()
    {
        FrameIndex = index,
        SwapchainId = 1,
        OutputW = w,
        OutputH = h,
        Qpc = 1_000_000 + index,
        MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs),
    };

    private static FlFrameRecord Unmeasured(uint index) => new()
    {
        FrameIndex = index,
        SwapchainId = 1,
        Qpc = 1_000_000 + index,
        MeasuredMask = (ushort)FlMeasured.PresentArgs,
    };

    [Fact]
    public void AStreamThatStartsUnmeasuredIsOneSegment()
    {
        List<FlFrameRecord> records =
        [
            Unmeasured(0),
            Unmeasured(1),
            .. Enumerable.Range(2, 8).Select(i => Measured((uint)i, 3840, 2160)),
        ];

        IReadOnlyList<Segment> segments = StreamSegmenter.Segment(records);

        segments.Should().ContainSingle("nothing about the settings changed; the writer merely went quiet first");
        segments[0].OutputW.Should().Be(3840);
        segments.Should().NotContain(s => s.OutputW == 0 && s.OutputH == 0,
            "a 0x0 segment is the writer's silence reported as a resolution");
    }

    [Fact]
    public void ARealResolutionChangeAfterAnUnmeasuredPrefixStillSplits()
    {
        // GREEN HALF: the baseline flag must not disable segmentation, only stop it firing on the
        // first measured record.
        List<FlFrameRecord> records =
        [
            Unmeasured(0),
            .. Enumerable.Range(1, 5).Select(i => Measured((uint)i, 3840, 2160)),
            .. Enumerable.Range(6, 5).Select(i => Measured((uint)i, 2560, 1440)),
        ];

        IReadOnlyList<Segment> segments = StreamSegmenter.Segment(records);

        segments.Should().HaveCount(2);
        segments[0].OutputW.Should().Be(3840);
        segments[1].OutputW.Should().Be(2560);
    }
}
