using FluentAssertions;
using FrameLedger.Domain.Detection;

namespace FrameLedger.Domain.Tests;

/// <summary>
/// What must invalidate a cached detection result, and what must not.
/// </summary>
/// <remarks>
/// 05_DETECTION §Caching triggers a re-run on an executable change or a rules
/// change. Each of those is a separate row here, because a key that ignores one
/// of them caches a stale answer indefinitely and there is no user-visible
/// symptom until somebody notices the library card is wrong.
/// </remarks>
public sealed class DetectionCacheKeyTests
{
    private static DetectionCacheKey Key(
        string path = @"C:\Games\Example\Game.exe",
        long size = 1024,
        long mtime = 1_700_000_000_000,
        string rules = "2026.08.1") =>
        new() { ExePath = path, SizeBytes = size, MtimeUnixMs = mtime, RulesVersion = rules };

    [Fact]
    public void AnUnchangedGameAndUnchangedRules_ReuseTheCachedResult() =>
        Key().Should().Be(Key());

    [Fact]
    public void ARebuiltExecutableOfTheSameSize_InvalidatesOnMtime() =>
        Key(mtime: 1_700_000_001_000).Should().NotBe(Key());

    [Fact]
    public void APatchedExecutableWithThePreservedTimestamp_InvalidatesOnSize() =>
        Key(size: 2048).Should().NotBe(Key());

    [Fact]
    public void ARulesUpdate_InvalidatesEveryCachedResult() =>
        Key(rules: "2026.09.1").Should().NotBe(Key());

    [Fact]
    public void AMovedInstall_IsADifferentKey() =>
        Key(path: @"D:\Games\Example\Game.exe").Should().NotBe(Key());
}
