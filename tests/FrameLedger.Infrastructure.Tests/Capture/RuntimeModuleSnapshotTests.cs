using FluentAssertions;
using FrameLedger.Application.Capture;
using FrameLedger.Infrastructure.Capture;

namespace FrameLedger.Infrastructure.Tests.Capture;

/// <summary>The out-of-process module snapshot: reads a real version, never throws.</summary>
public sealed class RuntimeModuleSnapshotTests
{
    [Fact]
    public void TakeOnOurOwnProcessReadsKernel32sVersion()
    {
        RuntimeModuleSet set = RuntimeModuleSnapshot.Take(Environment.ProcessId, ["kernel32.dll"]);

        set.Snapshots.Should().Be(1);
        set.Unreadable.Should().Be(0);
        set.Modules.Should().ContainSingle();
        RuntimeModuleInfo m = set.Modules[0];
        m.FileName.Should().BeEquivalentTo("kernel32.dll");
        m.Path.Should().EndWithEquivalentOf("kernel32.dll");
        m.Parsed.Should().NotBeNull("a system DLL carries a version resource");
        m.Parsed!.Major.Should().BeGreaterThanOrEqualTo(6);
        set.VersionOf("KERNEL32.DLL").Should().Be(m.Parsed);
    }

    [Fact]
    public void AModuleNotInTheAskedForSetIsNotReported()
    {
        RuntimeModuleSet set = RuntimeModuleSnapshot.Take(Environment.ProcessId, ["sl.interposer.dll"]);

        set.Unreadable.Should().Be(0);
        set.Modules.Should().BeEmpty("this test host does not load Streamline");
        set.VersionOf("sl.interposer.dll").Should().BeNull();
    }

    [Fact]
    public void TakeOnANonexistentPidIsUnreadableNotAThrow()
    {
        RuntimeModuleSet set = RuntimeModuleSnapshot.Take(int.MaxValue, ["kernel32.dll"]);

        set.Snapshots.Should().Be(1);
        set.Unreadable.Should().Be(1);
        set.Modules.Should().BeEmpty();
    }
}
