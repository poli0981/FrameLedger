using FluentAssertions;
using FrameLedger.Domain.Metrics;
using static FrameLedger.Domain.Tests.Metrics.SampleFixtures;

namespace FrameLedger.Domain.Tests.Metrics;

/// <summary>Sensor, VRAM and latency aggregates: the mean of what was measured, and N/A — never 0 — for what was not.</summary>
public sealed class AggregatesTests
{
    [Fact]
    public void SeriesAggregatesSkipNullsAndAreNullWithoutASample()
    {
        SeriesAggregates s = SeriesAggregates.Of([null, 60.0, null, 70.0, 65.0]);

        s.Count.Should().Be(3);
        s.Average.Should().Be(65);
        s.Max.Should().Be(70);

        SeriesAggregates none = SeriesAggregates.Of([null, null]);
        none.Count.Should().Be(0);
        none.Average.Should().BeNull();
        none.Max.Should().BeNull();
    }

    [Fact]
    public void VramAggregatesUseTheClaimingSamplesAndTheWritersBudget()
    {
        List<FrameSample> stream =
        [
            Present(1) with { VramUsedMb = 9000, Measured = MeasuredFields.Vram },
            Present(2) with { VramUsedMb = 7000, Measured = MeasuredFields.Vram },
            Present(3) with { VramUsedMb = 12345 },    // no claim: the value is nobody's
        ];

        VramAggregates v = VramAggregates.From(stream, budgetMb: 8000);

        v.Count.Should().Be(2);
        v.AverageMb.Should().Be(8000);
        v.MaxMb.Should().Be(9000);
        v.BudgetExceededPct.Should().Be(50);
    }

    [Fact]
    public void VramWithoutABudgetHasNoExceededShareAndWithoutSamplesNothing()
    {
        VramAggregates.From([Present(1) with { VramUsedMb = 9000, Measured = MeasuredFields.Vram }], budgetMb: null)
            .BudgetExceededPct.Should().BeNull("the writer published no budget, and 0 means nobody wrote it");

        VramAggregates none = VramAggregates.From(Stream(3), budgetMb: 8000);
        none.Count.Should().Be(0);
        none.AverageMb.Should().BeNull();
        none.MaxMb.Should().BeNull();
        none.BudgetExceededPct.Should().BeNull();
    }

    [Fact]
    public void LatencyAggregatesTakeMeanAndP95OverTheReflexSamples()
    {
        List<FrameSample> stream = [.. Enumerable.Range(1, 20).Select(i => Present((ulong)i) with
        {
            ReflexLatencyUs = (uint)(i * 1000),
            Measured = MeasuredFields.Latency,
        })];
        stream.Add(Present(99) with { ReflexLatencyUs = 0, Measured = MeasuredFields.Latency });    // 0 = unavailable
        stream.Add(Present(100) with { ReflexLatencyUs = 50_000 });                                  // unclaimed

        LatencyAggregates l = LatencyAggregates.From(stream);

        l.Count.Should().Be(20);
        l.AverageUs.Should().Be(10_500);
        l.P95Us.Should().BeApproximately(19_050, 1e-6, "rank 0.95 × 19 = 18.05 between 19,000 and 20,000");
    }

    [Fact]
    public void LatencyWithoutReflexIsNotApplicable()
    {
        LatencyAggregates l = LatencyAggregates.From(Stream(5));

        l.Count.Should().Be(0);
        l.AverageUs.Should().BeNull();
        l.P95Us.Should().BeNull();
    }
}
