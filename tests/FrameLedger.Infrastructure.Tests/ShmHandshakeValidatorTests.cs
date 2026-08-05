using System.Text;
using FluentAssertions;
using FrameLedger.Infrastructure.AntiCheat;
using FrameLedger.Shared;

namespace FrameLedger.Infrastructure.Tests;

/// <summary>
/// The refuse-to-attach check (<c>07_IPC</c> §Protocol rules, <c>04_CAPTURE</c> §Ring draining).
/// <para>
/// It was specified in two documents and implemented nowhere, because the input it needs — the Agent's
/// own build id — had no source at all (<c>20_OPEN_QUESTIONS</c> §S23-1). Every case below is driven,
/// including the ones that refuse: a gate whose red path is never exercised is unimplemented.
/// </para>
/// </summary>
public sealed class ShmHandshakeValidatorTests
{
    private const string _ours = "v0.1.0-42-gabc123def456";

    private static unsafe FlShmHandshake Handshake(
        string buildId,
        uint layoutVersion = ShmLayout.LayoutVersion,
        uint recordSize = 64,
        uint capacity = ShmLayout.DefaultCapacity)
    {
        FlShmHandshake h = default;
        h.LayoutVersion = layoutVersion;
        h.RecordSize = recordSize;
        h.Capacity = capacity;

        byte[] bytes = Encoding.ASCII.GetBytes(buildId);
        int n = Math.Min(bytes.Length, 31);
        for (int i = 0; i < n; i++)
        {
            h.BuildId[i] = bytes[i];
        }

        h.BuildId[n] = 0;
        return h;
    }

    [Fact]
    public void AMatchingHandshakeIsAccepted()
    {
        // The GREEN direction, asserted separately and first. A validator that refuses everything
        // carries exactly as much information as one that accepts everything.
        ShmHandshakeValidator.Validate(Handshake(_ours), _ours).Should().Be(ShmAttachRefusal.Ok);
    }

    [Fact]
    public void ADifferentBuildIdIsRefused()
    {
        // The case 04_CAPTURE describes: the app updated while the game was running, so the DLL inside
        // it is from another build. Tell the user to restart the game.
        ShmHandshakeValidator.Validate(Handshake("v0.1.0-43-gfeed0000beef"), _ours)
            .Should().Be(ShmAttachRefusal.BuildIdMismatch);
    }

    [Fact]
    public void AnUnpublishedHandshakeIsIncompleteAndNotAMismatch()
    {
        // layoutVersion is published LAST behind a release fence, so zero means "the Overlay has not
        // finished initialising" — a state to retry, not a version disagreement to report to the user.
        ShmHandshakeValidator.Validate(Handshake(_ours, layoutVersion: 0), _ours)
            .Should().Be(ShmAttachRefusal.Incomplete);
    }

    [Fact]
    public void AnUnknownLayoutVersionIsRefused()
    {
        ShmHandshakeValidator.Validate(Handshake(_ours, layoutVersion: ShmLayout.LayoutVersion + 1), _ours)
            .Should().Be(ShmAttachRefusal.LayoutVersionMismatch);
    }

    [Fact]
    public void TheVersionIsCheckedBeforeAnythingItVouchesFor()
    {
        // A handshake with an unknown version AND a wrong record size must report the VERSION, because
        // every other field is only meaningful under a layout both sides agree on — and the two
        // refusals mean different things to the user.
        ShmHandshakeValidator.Validate(
                Handshake(_ours, layoutVersion: ShmLayout.LayoutVersion + 1, recordSize: 48), _ours)
            .Should().Be(ShmAttachRefusal.LayoutVersionMismatch);
    }

    [Fact]
    public void AWrongRecordSizeIsRefusedEvenUnderTheRightVersion()
    {
        // Belt-and-braces against struct drift that did not bump the version, which is exactly the
        // mistake the mirror test exists to prevent and this catches at runtime if it ever ships.
        ShmHandshakeValidator.Validate(Handshake(_ours, recordSize: 48), _ours)
            .Should().Be(ShmAttachRefusal.RecordSizeMismatch);
    }

    [Theory]
    [InlineData(0u)]
    [InlineData(3u)]
    [InlineData(8191u)]
    public void ANonPowerOfTwoCapacityIsRefused(uint capacity)
    {
        // The ring masks indices with capacity-1. A non-power-of-two makes that arithmetic silently
        // address the wrong slots rather than fail, so it has to be refused at the door.
        ShmHandshakeValidator.Validate(Handshake(_ours, capacity: capacity), _ours)
            .Should().Be(ShmAttachRefusal.CapacityInvalid);
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    public void NoBuildIdOfOurOwnRefusesRatherThanMatching(string? mine)
    {
        // THE STATE THAT MADE THIS CHECK UNIMPLEMENTABLE. Before FlGuardBuildId existed the managed
        // side had no value here, and the obvious implementation — compare two strings — would have
        // compared "" with "" and reported a match forever. A gate that cannot fail, guarding the ABI.
        ShmHandshakeValidator.Validate(Handshake(_ours), mine).Should().Be(ShmAttachRefusal.Incomplete);
    }

    [Fact]
    public void AnEmptyBuildIdInTheHandshakeRefuses()
    {
        // The other half of the same trap: a producer that never wrote the field.
        ShmHandshakeValidator.Validate(Handshake(string.Empty), _ours).Should().Be(ShmAttachRefusal.Incomplete);
    }

    [Fact]
    public void TwoEmptyBuildIdsDoNotMatchEachOther()
    {
        // THE EXACT SHAPE THE FEATURE EXISTED IN FOR MONTHS, and the one the two cases above do not
        // reach between them: neither side has an id, string equality says they agree, and a gate whose
        // whole job is to detect version skew reports Ok for every process on the machine.
        //
        // §S23-1 described this as "compares '' with '' forever". It is worth its own case because a
        // suite can assert each half separately and still never put both halves in the same call —
        // which is what I did on the first draft of this file.
        ShmHandshakeValidator.Validate(Handshake(string.Empty), string.Empty)
            .Should().Be(ShmAttachRefusal.Incomplete);
        ShmHandshakeValidator.Validate(Handshake(string.Empty), null)
            .Should().Be(ShmAttachRefusal.Incomplete);
    }

    [Fact]
    public void DefaultIsNotPermission()
    {
        // A zeroed result must not read as "attach". Same rule AntiCheatVerdict follows: a value nobody
        // assigned has evaluated nothing and permits nothing.
        default(ShmAttachRefusal).Should().Be(ShmAttachRefusal.NotEvaluated);
        default(ShmAttachRefusal).Should().NotBe(ShmAttachRefusal.Ok);
    }

    [Fact]
    public void TheGuardReportsThisInstallsBuildId()
    {
        // Against the REAL FrameLedger.Guard.dll. This is the value 04_CAPTURE calls "our own", and
        // until 2026-08-05 there was no way to obtain it from managed code at all.
        string id = NativeAntiCheatGuard.BuildId();

        id.Should().NotBeNullOrEmpty(
            "an empty id refuses every attach, so a guard that cannot answer is a guard that disables capture");
        id.Length.Should().BeLessThan(32, "FlShmHandshake.buildId holds 31 characters plus NUL");

        // And it must actually work as the comparison input, not merely be non-empty.
        ShmHandshakeValidator.Validate(Handshake(id), id).Should().Be(ShmAttachRefusal.Ok);
        ShmHandshakeValidator.Validate(Handshake(id + "x"), id).Should().Be(ShmAttachRefusal.BuildIdMismatch);
    }
}
