using System.Reflection;
using FluentAssertions;
using FrameLedger.Domain.Detection;

namespace FrameLedger.Domain.Tests;

/// <summary>
/// The two rules that keep static detection from doing damage: it may not state
/// a runtime fact, and it may not overwrite a human.
/// </summary>
public sealed class StaticDetectionResultTests
{
    /// <summary>
    /// Names that would mean this type had started reporting what the game is
    /// DOING rather than what it IS.
    /// </summary>
    private static readonly string[] _runtimeFactFragments =
    [
        "upscaler", "fgmode", "framegen", "rtflag", "ptflag", "rrflag",
        "renderwidth", "renderheight", "outputwidth", "outputheight",
        "quality", "latency", "framerate", "fps",
    ];

    [Fact]
    public void NoMemberStatesARuntimeFact()
    {
        // 05_DETECTION: "a static hint may never set a runtime fact". The old
        // design blurred these and produced the field errors that motivated the
        // whole rewrite, so the boundary is enforced rather than described.
        foreach (PropertyInfo p in typeof(StaticDetectionResult).GetProperties())
        {
            foreach (string fragment in _runtimeFactFragments)
            {
                p.Name.Should().NotContainEquivalentOf(fragment,
                    $"StaticDetectionResult.{p.Name} looks like a runtime fact; static hints populate " +
                    "library metadata and capability chips, never sessions.*");
            }
        }
    }

    [Fact]
    public void CapabilityIdsAreShippedHints_NotMeasurements()
    {
        // The type-level half of the same rule: capabilities are ids of things
        // the game SHIPS. If this ever becomes a richer type carrying, say, a
        // factor or a preset, that is the moment to re-read 05_DETECTION.
        PropertyInfo caps = typeof(StaticDetectionResult).GetProperty(nameof(StaticDetectionResult.CapabilityIds))!;

        caps.PropertyType.Should().Be<IReadOnlyList<string>>();
    }

    // ---- never overwrite a human -------------------------------------------

    [Fact]
    public void ARerunMayOverwriteAValueItDetectedItself() =>
        StaticDetectionResult.ShouldWrite(DetectionProvenance.Detected, "unity").Should().BeTrue();

    [Fact]
    public void ARerunMayNotOverwriteAUserSuppliedValue() =>
        StaticDetectionResult.ShouldWrite(DetectionProvenance.UserSupplied, "unity").Should().BeFalse();

    [Fact]
    public void UnknownProvenanceIsTreatedAsUserSupplied()
    {
        // The safe direction. Of the two ways to be wrong, badging a human's
        // typed value as auto-detected is a lie they cannot see through;
        // failing to badge a detected value merely costs a badge.
        StaticDetectionResult.ShouldWrite(DetectionProvenance.Unknown, "unity").Should().BeFalse();
        ((int)DetectionProvenance.Unknown).Should().Be(0, "a default must land on the cautious side");
    }

    [Fact]
    public void NothingDetectedNeverOverwritesAnything()
    {
        // "Not established" is not "absent". Writing null over a good value on a
        // run where the probe simply could not look would lose data.
        StaticDetectionResult.ShouldWrite(DetectionProvenance.Detected, null).Should().BeFalse();
        StaticDetectionResult.ShouldWrite(DetectionProvenance.UserSupplied, null).Should().BeFalse();
    }
}
