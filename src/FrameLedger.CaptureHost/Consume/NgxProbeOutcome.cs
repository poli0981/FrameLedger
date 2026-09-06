namespace FrameLedger.CaptureHost.Consume;

/// <summary>What one run of <c>fl-probe-nvapi --ngx-state</c> came back with.</summary>
internal enum NgxProbeOutcome
{
    /// <summary>The loop was built without a probe, or the probe never ran.</summary>
    NotRun,

    /// <summary>The driver answered for this pid; the masks are the driver's.</summary>
    Answered,

    /// <summary>NvAPI answered but not for this pid: not NVIDIA-rendered, an old driver, or the API refusing the caller.</summary>
    Unanswered,

    /// <summary>No usable NVIDIA driver on this machine.</summary>
    Degraded,

    /// <summary>The probe binary was not beside this host.</summary>
    ProbeMissing,

    /// <summary>The probe could not be started, timed out, or printed nothing this host understands.</summary>
    ProbeFailed,
}
