namespace FrameLedger.Application.Capture;

/// <summary>
/// How many drain ticks the loop ran, and on how many of them the target owned the
/// foreground window.
/// </summary>
/// <remarks>
/// <para>
/// <b>Two numbers, because one cannot be read.</b> "Unfocused for 40 ticks" is a finding only
/// if the target ever HAD the foreground; a process owning no top-level window at all — which
/// <c>hook-harness</c> is, presenting to a composition swapchain — is unfocused on every tick
/// of every run and means nothing by it. The pair separates the two, and
/// <c>SessionReport</c> is required to keep them separate.
/// </para>
/// <para>
/// <b>Why the capture host measures this at all.</b> Frame generation stops while a title is
/// unfocused. Measured 2026-08-16 on Cyberpunk 2077: an alt-tab during a ×2 capture mixed
/// intervals at 2.00 with intervals near 1.00 and the achieved <c>presents / batch</c> came
/// out at <b>1.84</b> — wrong by 8%, with no diagnostic anywhere. <c>FgWindow.BatchRefusal</c>
/// is what CATCHES that, from the records alone; this is what NAMES it.
/// </para>
/// </remarks>
public sealed class FocusTally
{
    /// <summary>Drain ticks completed.</summary>
    public long Ticks { get; private set; }

    /// <summary>Of those, the ones on which the target owned the foreground window.</summary>
    public long Foreground { get; private set; }

    /// <summary>Records one tick's observation.</summary>
    public void Sample(bool isForeground)
    {
        Ticks++;
        if (isForeground)
        {
            Foreground++;
        }
    }
}
