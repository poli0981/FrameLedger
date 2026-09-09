using FluentAssertions;

namespace FrameLedger.Infrastructure.Tests.Ipc;

/// <summary>
/// <c>04_CAPTURE</c> §Threading model: the session loop is the only thing that touches the ring, and
/// <c>ShmRingReader</c> is the only thing that opens it. Now that the capture path ships (P2 PR-C),
/// a second reader — a diagnostic, a UI peek, a "just read the header" helper — would be a second
/// party on a single-consumer ring, so the source tree is swept for one.
/// </summary>
public sealed class NoSecondRingReaderTests
{
    private static string RepoRoot()
    {
        string? dir = AppContext.BaseDirectory;
        while (dir is not null && !File.Exists(Path.Combine(dir, "FrameLedger.slnx")))
        {
            dir = Path.GetDirectoryName(dir);
        }

        return dir ?? throw new InvalidOperationException("FrameLedger.slnx not found above the test binary");
    }

    [Fact]
    public void OnlyShmRingReaderOpensTheRingMapping()
    {
        string src = Path.Combine(RepoRoot(), "src");
        string[] offenders = Directory.EnumerateFiles(src, "*.cs", SearchOption.AllDirectories)
            .Where(f => !f.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}", StringComparison.Ordinal))
            .Where(f => !f.EndsWith("ShmRingReader.cs", StringComparison.Ordinal))
            .Where(f =>
            {
                string text = File.ReadAllText(f);
                return text.Contains("MemoryMappedFile.OpenExisting", StringComparison.Ordinal)
                    || text.Contains(@"FrameLedger.Ring.", StringComparison.Ordinal);
            })
            .Select(f => Path.GetRelativePath(src, f))
            .ToArray();

        offenders.Should().BeEmpty("ShmRingReader is the single consumer; a second mapping of the ring is a second reader");
    }
}
