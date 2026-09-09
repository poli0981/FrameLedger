using FluentAssertions;
using FrameLedger.Infrastructure.Capture;

namespace FrameLedger.Infrastructure.Tests.Capture;

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
    public void TwoUntypedProcessesAreTwoInstancesAndRefuse()
    {
        ChromiumGpuProcess.Pick(
        [
            (100, @"""D:\g\Game.exe"""),
            (101, @"""D:\g\Game.exe"""),
        ]).Should().BeNull("two instances of an ordinary game are as ambiguous as they were");
    }

    [Fact]
    public void TheNwJsShapeResolvesToTheUntypedBrowserProcess()
    {
        // MEASURED 2026-09-03, Flower in Us: six processes and NO --type=gpu-process. NW.js runs the
        // GPU in-process, in the browser -- the one process Chromium does not type.
        ChromiumGpuProcess.Pick(
        [
            (13660, @"""D:\g\Game.exe"" --nwapp=D:\g"),
            (39396, @"""D:\g\Game.exe"" --type=crashpad-handler --database=x"),
            (40272, @"""D:\g\Game.exe"" --type=utility --utility-sub-type=network.mojom.NetworkService"),
            (35972, @"""D:\g\Game.exe"" --type=utility --utility-sub-type=storage.mojom.StorageService"),
            (19068, @"""D:\g\Game.exe"" --type=renderer --nwjs"),
            (4412, @"""D:\g\Game.exe"" --type=utility --utility-sub-type=audio.mojom.AudioService"),
        ], out ChromiumGpuProcess.Kind kind).Should().Be(13660);
        kind.Should().Be(ChromiumGpuProcess.Kind.BrowserWithInProcessGpu);
    }

    [Fact]
    public void AGpuProcessWinsOverTheBrowserWhenBothExist()
    {
        ChromiumGpuProcess.Pick(
        [
            (1, @"""D:\g\Game.exe"""),
            (2, @"""D:\g\Game.exe"" --type=gpu-process"),
            (3, @"""D:\g\Game.exe"" --type=renderer"),
        ], out ChromiumGpuProcess.Kind kind).Should().Be(2);
        kind.Should().Be(ChromiumGpuProcess.Kind.GpuProcess);
    }

    [Fact]
    public void TwoBrowsersBesideTypedChildrenAreTwoInstancesAndRefuse()
    {
        ChromiumGpuProcess.Pick(
        [
            (1, @"""D:\g\Game.exe"""),
            (2, @"""D:\g\Game.exe"" --type=renderer"),
            (3, @"""D:\g\Game.exe"""),
            (4, @"""D:\g\Game.exe"" --type=renderer"),
        ]).Should().BeNull();
    }

    [Fact]
    public void ASingleProcessIsNotAChromiumTreeAndIsNotThisClassToDecide()
    {
        // One untyped candidate alone is the ordinary single-instance case; TargetResolver
        // resolves it before ever asking here, and this class must not claim it as a browser.
        ChromiumGpuProcess.Pick([(1, @"""D:\g\Game.exe""")], out ChromiumGpuProcess.Kind kind).Should().BeNull();
        kind.Should().Be(ChromiumGpuProcess.Kind.None);
    }

    [Fact]
    public void DescribeNamesEveryKindForTheRefusalLine()
    {
        ChromiumGpuProcess.Describe(
        [
            (1, @"""D:\g\Game.exe"""),
            (2, @"""D:\g\Game.exe"" --type=utility"),
            (3, @"""D:\g\Game.exe"" --type=utility"),
            (4, null),
        ]).Should().Be("browser=1, unreadable=1, utility=2");
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
