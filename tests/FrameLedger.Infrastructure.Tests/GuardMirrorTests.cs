using FluentAssertions;
using FrameLedger.Domain.AntiCheat;
using FrameLedger.Infrastructure.AntiCheat;

namespace FrameLedger.Infrastructure.Tests;

/// <summary>
/// Proves the managed <see cref="AntiCheatRefusalReason"/> has not drifted from
/// the native <c>fl::guard::Reason</c>.
/// </summary>
/// <remarks>
/// <para>
/// This is the test <c>20_OPEN_QUESTIONS</c> §S15 item 1 exists for. The
/// managed side is a facade, but a facade whose enum has silently diverged
/// shows the user the WRONG refusal — "a driver was found" when a service was —
/// and, worse, could map an unknown value onto <c>Allow</c>.
/// </para>
/// <para>
/// Same discipline as the shm struct mirror: the two sides are compared
/// mechanically rather than by review, because struct and enum drift between
/// the layers is the most dangerous silent bug in this architecture.
/// </para>
/// </remarks>
public sealed class GuardMirrorTests
{
    [Fact]
    public void TheManagedEnumHasExactlyAsManyValuesAsTheNativeOne()
    {
        int managed = Enum.GetValues<AntiCheatRefusalReason>().Length;

        NativeAntiCheatGuard.NativeReasonCount().Should().Be(managed,
            "a value added on one side and forgotten on the other maps a refusal onto the wrong reason");
    }

    [Fact]
    public void EveryManagedReasonNamesTheSameThingNatively()
    {
        foreach (AntiCheatRefusalReason reason in Enum.GetValues<AntiCheatRefusalReason>())
        {
            string native = NativeAntiCheatGuard.NativeReasonName((int)reason);

            native.Should().Be(reason.ToString(),
                $"managed {reason} = {(int)reason} must be the same code the native guard calls {native}");
        }
    }

    [Fact]
    public void AnOutOfRangeCodeReturnsNothing_RatherThanAPlausibleName()
    {
        // Returning "Unknown" would let drift look like a legitimate value.
        NativeAntiCheatGuard.NativeReasonName(-1).Should().BeEmpty();
        NativeAntiCheatGuard.NativeReasonName(9999).Should().BeEmpty();
    }

    [Fact]
    public void AllowIsZeroOnBothSides()
    {
        // The value the whole gate turns on.
        ((int)AntiCheatRefusalReason.Allow).Should().Be(0);
        NativeAntiCheatGuard.NativeReasonName(0).Should().Be("Allow");
    }
}
