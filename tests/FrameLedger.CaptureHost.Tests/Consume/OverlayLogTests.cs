using FluentAssertions;
using FrameLedger.CaptureHost.Consume;

namespace FrameLedger.CaptureHost.Tests.Consume;

public sealed class OverlayLogTests : IDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "fl-ovlog-" + Guid.NewGuid().ToString("N"));

    public OverlayLogTests() => Directory.CreateDirectory(_dir);

    public void Dispose()
    {
        try
        {
            Directory.Delete(_dir, recursive: true);
        }
        catch (IOException)
        {
        }
    }

    [Fact]
    public void TheNewestLogForThePidIsFoundAndOnlyTheLinesWorthReadingArePrinted()
    {
        File.WriteAllText(Path.Combine(_dir, "overlay-4242-20260906-120000.log"), "# old\n+0.001s  RING_CREATED     x a=0 b=0x0\n");
        string newer = Path.Combine(_dir, "overlay-4242-20260906-130000.log");
        File.WriteAllLines(newer,
        [
            "# FrameLedger.Overlay build abc pid 4242 layout v3 image game.exe",
            "+     0.001s  RING_CREATED     Local\\FrameLedger.Ring.<pid>              a=8192 b=0x0",
            "+     0.020s  HOOK_INSTALLED   dxgi!IDXGISwapChain::Present             a=0 b=0x7ff0",
            "+     3.400s  FAULT            Hook_Present                             a=3221225477 b=0x0",
            "+     9.000s  STOP             observing stopped                        a=3 b=0x0",
            "+     9.001s  UNHOOK_RESTORED  dxgi!IDXGISwapChain::Present             a=1 b=0x7ff0",
            "+     9.001s  UNHOOK_DECLINED  kernelbase!LoadLibraryExW                a=0 b=0x7ff1",
        ]);
        File.SetLastWriteTimeUtc(newer, DateTime.UtcNow.AddMinutes(1));
        File.WriteAllText(Path.Combine(_dir, "overlay-9999-20260906-140000.log"), "# other pid\n");

        OverlayLog.Find(4242, _dir).Should().Be(newer);
        List<string> lines = [.. OverlayLog.Describe(4242, _dir)];

        lines[0].Should().Contain(newer).And.Contain("(6 event(s))");
        lines.Should().HaveCount(5, "the header, then FAULT, STOP and the two unhook decisions; the installs stay in the file");
        lines[1].Should().Contain("FAULT");
        lines[2].Should().Contain("STOP");
        lines[3].Should().Contain("UNHOOK_RESTORED");
        lines[4].Should().Contain("UNHOOK_DECLINED");
    }

    [Fact]
    public void NoLogIsSaidPlainly()
    {
        OverlayLog.Find(1, _dir).Should().BeNull();
        OverlayLog.Find(1, Path.Combine(_dir, "missing")).Should().BeNull();
        List<string> lines = [.. OverlayLog.Describe(1, _dir)];
        lines.Should().ContainSingle().Which.Should().Contain("none found");
    }
}
