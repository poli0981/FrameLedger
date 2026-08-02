using FluentAssertions;
using FrameLedger.Domain.AntiCheat;

namespace FrameLedger.Domain.Tests;

public sealed class AntiCheatVerdictTests
{
    [Fact]
    public void DefaultConstructed_IsNotAllowed()
    {
        // If a code path ever forgets to assign a verdict, what it leaves
        // behind must not read as permission. This is the managed half of the
        // static_assert on the native side.
        default(AntiCheatVerdict).IsAllowed.Should().BeFalse();
    }

    [Fact]
    public void Allowed_IsTheOnlyPermittingValue()
    {
        AntiCheatVerdict.Allowed().IsAllowed.Should().BeTrue();

        foreach (AntiCheatRefusalReason reason in Enum.GetValues<AntiCheatRefusalReason>())
        {
            if (reason == AntiCheatRefusalReason.Allow)
            {
                continue;
            }

            AntiCheatVerdict.Refused(reason, "fam", "sig").IsAllowed.Should().BeFalse();
        }
    }

    [Fact]
    public void Refused_CannotCarryAllow()
    {
        // Making the contradiction unrepresentable rather than merely untested.
        Action act = () => AntiCheatVerdict.Refused(AntiCheatRefusalReason.Allow, "fam", "sig");
        act.Should().Throw<ArgumentOutOfRangeException>();
    }

    [Fact]
    public void FromNative_MapsEveryKnownReason()
    {
        foreach (AntiCheatRefusalReason reason in Enum.GetValues<AntiCheatRefusalReason>())
        {
            AntiCheatVerdict v = AntiCheatVerdict.FromNative((int)reason, "fam", "sig");
            v.Reason.Should().Be(reason);
        }
    }

    [Theory]
    [InlineData(-1)]
    [InlineData(9999)]
    public void FromNative_RefusesAnUnrecognisedCode(int unknown)
    {
        // An unrecognised code means the mirror has drifted from the native
        // enum. A gate that does not understand its own answer must not allow.
        AntiCheatVerdict v = AntiCheatVerdict.FromNative(unknown, "fam", "sig");

        v.IsAllowed.Should().BeFalse();
        v.Signal.Should().Contain("drifted");
    }

    [Fact]
    public void Allowed_CarriesNoSignal()
    {
        AntiCheatVerdict v = AntiCheatVerdict.FromNative(0, "ignored", "ignored");

        v.IsAllowed.Should().BeTrue();
        v.Family.Should().BeEmpty();
        v.Signal.Should().BeEmpty();
    }
}
