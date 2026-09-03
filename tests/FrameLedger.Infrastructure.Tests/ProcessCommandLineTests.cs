using System.Diagnostics;
using FluentAssertions;
using FrameLedger.Infrastructure.Io;

namespace FrameLedger.Infrastructure.Tests;

/// <summary>
/// The kernel-served command line, against processes whose command lines are known.
/// </summary>
public sealed class ProcessCommandLineTests
{
    [Fact]
    public void OurOwnCommandLineComesBackRawAndNamesOurImage()
    {
        // The kernel hands back the string as it was at creation, quoting included — measured:
        // `"E:\...	esthost.exe" ...` — while Environment.CommandLine is the runtime's
        // re-joined, unquoted form, so the two are not byte-equal and the first draft of this
        // test asserted that they were. What is invariant is that the raw line names our image.
        string? read = ProcessCommandLine.TryRead(Environment.ProcessId);

        read.Should().NotBeNull();
        read.Should().Contain(Path.GetFileNameWithoutExtension(Environment.ProcessPath),
            "the command line starts with the image that is running");
    }

    [Fact]
    public void AChildProcessCommandLineIsReadableAndCarriesItsArguments()
    {
        Process p = Process.Start(new ProcessStartInfo("cmd.exe", "/c ping -n 30 127.0.0.1 > nul & rem --type=gpu-process")
        {
            UseShellExecute = false,
            CreateNoWindow = true,
        })!;
        try
        {
            string? read = ProcessCommandLine.TryRead(p.Id);

            read.Should().NotBeNull();
            read.Should().Contain("--type=gpu-process");
            read.Should().Contain("ping -n 30");
        }
        finally
        {
            p.Kill(entireProcessTree: true);
            p.Dispose();
        }
    }

    [Fact]
    public void APidThatDoesNotExistIsNullNotEmpty()
    {
        // Null is "could not read"; empty is a real answer. A caller narrowing on the difference
        // must be able to tell them apart.
        ProcessCommandLine.TryRead(0).Should().BeNull();
        ProcessCommandLine.TryRead(-1).Should().BeNull();
        ProcessCommandLine.TryRead(int.MaxValue - 1).Should().BeNull();
    }
}
