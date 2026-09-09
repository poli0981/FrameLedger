using FluentAssertions;
using FrameLedger.Infrastructure.Recording;

namespace FrameLedger.Infrastructure.Tests.Recording;

/// <summary>The real Application log: unprivileged, bounded by the window, and false rather than a throw for a name nobody crashed.</summary>
public sealed class EventLogCrashSourceTests
{
    [Fact]
    public void AnExecutableNobodyCrashedInTheLastMinuteIsNotACrash()
    {
        var source = new EventLogCrashSource();
        DateTimeOffset now = DateTimeOffset.UtcNow;

        bool found = source.FoundCrash("fl-nobody-ever-ran-this-" + Guid.NewGuid().ToString("N") + ".exe", now.AddMinutes(-1), now.AddSeconds(30));

        found.Should().BeFalse();
    }
}
