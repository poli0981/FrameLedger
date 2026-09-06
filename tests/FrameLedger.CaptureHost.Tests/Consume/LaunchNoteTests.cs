using FluentAssertions;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

public sealed class LaunchNoteTests
{
    [Fact]
    public void AnAttachSessionPrintsOnlyTheBeforeHookCount()
    {
        List<string> lines = [.. LaunchNote.Describe(null, new FlWriterState { DxgiPresentsBeforeHook = 137 })];

        lines.Should().ContainSingle().Which.Should().Contain("presents before the first hooked present: 137");
    }

    [Fact]
    public void ALaunchedSessionPrintsTheWaitFirst()
    {
        List<string> lines = [.. LaunchNote.Describe(TimeSpan.FromMilliseconds(842), new FlWriterState { DxgiPresentsBeforeHook = 0 })];

        lines.Should().HaveCount(2);
        lines[0].Should().Contain("injected 842 ms after the process was started");
        lines[1].Should().Contain("presents before the first hooked present: 0");
    }

    [Fact]
    public void TheNotReadSentinelIsNamedAndNeverPrintedAsANumber()
    {
        var writer = new FlWriterState { DxgiPresentsBeforeHook = FlWriterState.DxgiPresentsBeforeHookNotRead };

        List<string> lines = [.. LaunchNote.Describe(null, writer)];

        lines.Should().ContainSingle().Which.Should().Contain("not read").And.NotContain("4294967295");
    }
}
