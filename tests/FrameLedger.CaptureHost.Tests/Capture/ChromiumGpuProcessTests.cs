using FluentAssertions;
using FrameLedger.CaptureHost.Capture;

namespace FrameLedger.CaptureHost.Tests.Capture;

/// <summary>
/// The only discriminator the resolver may apply to an ambiguous image, pinned in both
/// directions: exactly one GPU process resolves, everything else still refuses.
/// </summary>
public sealed class ChromiumGpuProcessTests
{
    [Fact]
    public void ExactlyOneGpuProcessResolves()
    {
        ChromiumGpuProcess.Pick(
        [
            (100, @"""D:\g\Game.exe"" --nwapp=D:\g --type=renderer --field-trial-handle=1"),
            (101, @"""D:\g\Game.exe"" --type=gpu-process --gpu-preferences=abc"),
            (102, @"""D:\g\Game.exe"" --nwapp=D:\g"),
        ]).Should().Be(101);
    }

    [Fact]
    public void NoGpuProcessStillRefuses()
    {
        ChromiumGpuProcess.Pick(
        [
            (100, @"""D:\g\Game.exe"""),
            (101, @"""D:\g\Game.exe"" --type=renderer"),
        ]).Should().BeNull("two instances of an ordinary game are as ambiguous as they were");
    }

    [Fact]
    public void TwoGpuProcessesRefuseBecauseTwoInstancesAreRunning()
    {
        ChromiumGpuProcess.Pick(
        [
            (100, @"""D:\g\Game.exe"" --type=gpu-process"),
            (101, @"""D:\g\Game.exe"" --type=gpu-process"),
        ]).Should().BeNull();
    }

    [Fact]
    public void AnUnreadableCandidateMustNotNarrowTheSet()
    {
        // The same rule TargetResolver applies to an unreadable image path: "could not look" is
        // not "is not the target". One readable GPU process beside a process we could not read
        // is not a resolution — the unreadable one could be a second GPU process.
        ChromiumGpuProcess.Pick(
        [
            (100, @"""D:\g\Game.exe"" --type=gpu-process"),
            (101, null),
        ]).Should().BeNull();
    }

    [Theory]
    [InlineData("--type=gpu-process", true)]
    [InlineData("\"--type=gpu-process\"", true)]
    [InlineData("--type=gpu-process-foo", false)]
    [InlineData("--type=gpu", false)]
    [InlineData("--gpu-process", false)]
    [InlineData("x --type=gpu-processy", false)]
    public void TheMarkerIsAWholeArgument(string commandLine, bool expected)
    {
        ChromiumGpuProcess.HasMarker(commandLine).Should().Be(expected);
    }
}
