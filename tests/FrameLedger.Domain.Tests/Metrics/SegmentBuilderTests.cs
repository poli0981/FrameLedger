using FluentAssertions;
using FrameLedger.Domain.Metrics;
using static FrameLedger.Domain.Tests.Metrics.SampleFixtures;

namespace FrameLedger.Domain.Tests.Metrics;

/// <summary>Stream first, settings second — and the writer's silence is never a resolution.</summary>
public sealed class SegmentBuilderTests
{
    private static FrameSample At(uint index, uint swapchain, ushort w = 3840, ushort h = 2160) =>
        Present(1_000_000 + index, swapchain, w, h);

    private static FrameSample Unmeasured(uint index) => new()
    {
        SwapchainId = 1,
        Qpc = 1_000_000 + index,
        Measured = MeasuredFields.PresentArgs,
    };

    [Fact]
    public void StreamsAreSeparatedBySwapchainBeforeResolution()
    {
        // ONE hook sees EVERY swapchain in the process. Two interleaved streams at different sizes are ONE
        // segment each; splitting on resolution first would cut a segment on every alternation.
        List<FrameSample> samples = [];
        for (uint i = 0; i < 20; i++)
        {
            samples.Add(At(i * 2, swapchain: 1, w: 3840, h: 2160));
            samples.Add(At((i * 2) + 1, swapchain: 2, w: 512, h: 128));
        }

        IReadOnlyList<Segment> segments = SegmentBuilder.Build(samples);

        segments.Should().HaveCount(2, "two streams, neither of which changed resolution");
        segments.Select(s => s.SwapchainId).Should().BeEquivalentTo([1u, 2u]);
        segments[0].Samples.Should().HaveCount(20);
    }

    [Fact]
    public void AResolutionChangeWithinOneStreamSplitsIt()
    {
        List<FrameSample> samples =
        [
            .. Enumerable.Range(0, 10).Select(i => At((uint)i, 1, 3840, 2160)),
            .. Enumerable.Range(10, 10).Select(i => At((uint)i, 1, 2560, 1440)),
        ];

        IReadOnlyList<Segment> segments = SegmentBuilder.Build(samples);

        segments.Should().HaveCount(2);
        segments[0].OutputW.Should().Be(3840);
        segments[1].OutputW.Should().Be(2560);
        segments[1].OutputH.Should().Be(1440);
    }

    [Fact]
    public void AnUnmeasuredSizeDoesNotSplitASegment()
    {
        List<FrameSample> samples = [.. Enumerable.Range(0, 10).Select(i => At((uint)i, 1))];
        samples[5] = samples[5] with { OutputW = 0, OutputH = 0, Measured = MeasuredFields.PresentArgs };

        SegmentBuilder.Build(samples).Should().ContainSingle();
    }

    [Fact]
    public void AStreamThatStartsUnmeasuredIsOneSegment()
    {
        // The unmeasured-prefix case the segmenter once got wrong: w/h start at 0 and were both "no baseline
        // yet" and a real resolution, so the split fired on the first measured sample and emitted a 0×0 segment.
        List<FrameSample> samples =
        [
            Unmeasured(0),
            Unmeasured(1),
            .. Enumerable.Range(2, 8).Select(i => At((uint)i, 1, 3840, 2160)),
        ];

        IReadOnlyList<Segment> segments = SegmentBuilder.Build(samples);

        segments.Should().ContainSingle("nothing about the settings changed; the writer merely went quiet first");
        segments[0].OutputW.Should().Be(3840);
        segments.Should().NotContain(s => s.OutputW == 0 && s.OutputH == 0, "a 0x0 segment is the writer's silence reported as a resolution");
    }

    [Fact]
    public void ARealResolutionChangeAfterAnUnmeasuredPrefixStillSplits()
    {
        List<FrameSample> samples =
        [
            Unmeasured(0),
            .. Enumerable.Range(1, 5).Select(i => At((uint)i, 1, 3840, 2160)),
            .. Enumerable.Range(6, 5).Select(i => At((uint)i, 1, 2560, 1440)),
        ];

        IReadOnlyList<Segment> segments = SegmentBuilder.Build(samples);

        segments.Should().HaveCount(2);
        segments[0].OutputW.Should().Be(3840);
        segments[1].OutputW.Should().Be(2560);
    }

    [Fact]
    public void SwapchainIdZeroIsNeverTheDominantStreamWhileARealOneExists()
    {
        List<FrameSample> samples =
        [
            .. Enumerable.Range(0, 50).Select(i => At((uint)i, 0)),
            .. Enumerable.Range(50, 5).Select(i => At((uint)i, 7)),
        ];

        SegmentBuilder.DominantStream(samples).Should().OnlyContain(s => s.SwapchainId == 7);
    }

    [Fact]
    public void TheUnidentifiedStreamIsUsedWhenThereIsNothingElse()
    {
        List<FrameSample> samples = [.. Enumerable.Range(0, 5).Select(i => At((uint)i, 0))];

        SegmentBuilder.DominantStream(samples).Should().HaveCount(5);
    }

    [Fact]
    public void TheDominantStreamIsTheLargestIdentifiedOne()
    {
        List<FrameSample> samples =
        [
            .. Enumerable.Range(0, 5).Select(i => At((uint)i, 1)),
            .. Enumerable.Range(5, 9).Select(i => At((uint)i, 2)),
        ];

        SegmentBuilder.DominantStream(samples).Should().HaveCount(9).And.OnlyContain(s => s.SwapchainId == 2);
    }

    [Fact]
    public void TheGenericFormAppliesTheSameRuleToAnyRecord()
    {
        // The capture host keeps the shared-memory record and asks the same question of it.
        (uint Chain, int N)[] records = [(0u, 1), (0u, 2), (0u, 3), (5u, 4)];

        SegmentBuilder.DominantStream(records, r => r.Chain).Should().Equal((5u, 4));
    }

    [Fact]
    public void AnEmptyRingProducesNothing()
    {
        SegmentBuilder.DominantStream([]).Should().BeEmpty();
        SegmentBuilder.Build([]).Should().BeEmpty();
    }
}
