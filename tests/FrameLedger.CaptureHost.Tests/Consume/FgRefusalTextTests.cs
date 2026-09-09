using FluentAssertions;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Domain.Metrics;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>
/// The English for each <see cref="FgRefusalKind"/>, byte-identical to what the report printed while the
/// arithmetic lived in this host — so the port changed no line a verification run has read.
/// </summary>
public sealed class FgRefusalTextTests
{
    [Fact]
    public void NothingRefusedIsNoText() => FgRefusalText.Describe(null).Should().BeNull();

    [Theory]
    [InlineData(FgRefusalKind.NotCounted, "no record claims FL_MEASURED_FG_COUNTS, so nothing counted evaluations")]
    [InlineData(FgRefusalKind.NoEvaluations, "no application-frame token was counted in the window")]
    [InlineData(FgRefusalKind.NoBatches, "no present drained a Streamline batch, so there is no ratio to read")]
    public void TheFixedSentences(FgRefusalKind kind, string expected) =>
        FgRefusalText.Describe(new FgRefusal(kind, FgRefusalSubject.Factor)).Should().StartWith(expected);

    [Fact]
    public void AttributionNamesTheCountAndTheSubject()
    {
        FgRefusalText.Describe(new FgRefusal(FgRefusalKind.Unattributed, FgRefusalSubject.Factor, Count: 3))
            .Should().Be("3 record(s) carry swapchainId 0, so the presents cannot be attributed and the ratio has no denominator anyone can name");
        FgRefusalText.Describe(new FgRefusal(FgRefusalKind.Unattributed, FgRefusalSubject.PresentsPerBatch, Count: 3))
            .Should().Be("3 record(s) carry swapchainId 0, so the presents cannot be attributed");
        FgRefusalText.Describe(new FgRefusal(FgRefusalKind.MultipleStreams, FgRefusalSubject.Factor, Count: 2))
            .Should().Contain("2 swapchains presented in the window").And.Contain("an evaluation belonging to one stream");
        FgRefusalText.Describe(new FgRefusal(FgRefusalKind.MultipleStreams, FgRefusalSubject.PresentsPerBatch, Count: 2))
            .Should().Contain("a batch belonging to one stream");
    }

    [Fact]
    public void TheSentinelsNameTheByteAndTheCount()
    {
        FgRefusalText.Describe(new FgRefusal(FgRefusalKind.CountSaturated, FgRefusalSubject.Factor, Count: 40))
            .Should().Be("40 record(s) hit the fgEvaluations ceiling of 255, which is a saturation sentinel rather than a count — dividing by it would report a floor");
        FgRefusalText.Describe(new FgRefusal(FgRefusalKind.DxgiSaturated, FgRefusalSubject.Factor, Count: 1))
            .Should().Contain("1 record(s) hit the dxgiUnseen ceiling of 255").And.Contain("DXGI counted more presents than the byte can carry");
    }

    [Fact]
    public void TheUniformityRefusalsCarryTheNumbers()
    {
        FgRefusalText.Describe(new FgRefusal(FgRefusalKind.TooShortToCheck, FgRefusalSubject.Factor, Count: 32))
            .Should().Be("the window holds 32 record(s), below the 64 needed to check whether the frame-generation state changed during it");
        FgRefusalText.Describe(new FgRefusal(FgRefusalKind.TooShortToCheck, FgRefusalSubject.PresentsPerBatch, Count: 32))
            .Should().Contain("whether the presents-per-batch ratio changed during it");
        FgRefusalText.Describe(new FgRefusal(FgRefusalKind.NonUniform, FgRefusalSubject.Factor,
                BucketIndex: 7, BucketCount: 8, BucketValue: 1.96, Overall: 2.85))
            .Should().Be("the frame-generation state changed during the session — bucket 8 of 8 measures 1.96 against 2.85 overall, and a session-level number would describe a configuration that never existed");
        FgRefusalText.Describe(new FgRefusal(FgRefusalKind.NonUniform, FgRefusalSubject.PresentsPerBatch,
                BucketIndex: 0, BucketCount: 8, BucketValue: double.PositiveInfinity, Overall: 2))
            .Should().Contain("the presents-per-batch ratio changed during the session — bucket 1 of 8 measures no evaluations against 2 overall");
        FgRefusalText.Describe(new FgRefusal(FgRefusalKind.AmbiguousBand, FgRefusalSubject.Factor, Overall: 1.25))
            .Should().Be("presents/tokens = 1.25 sits between the `none` ceiling (1.05) and the cadence threshold (1.5) — not a configuration this consumer can name");
    }
}
