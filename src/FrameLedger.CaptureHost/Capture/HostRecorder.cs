using FrameLedger.Application.Capture;
using FrameLedger.Application.Consent;
using FrameLedger.Application.Recording;
using FrameLedger.Application.Telemetry;
using FrameLedger.Infrastructure.Blobs;
using FrameLedger.Infrastructure.Persistence;
using FrameLedger.Infrastructure.Recording;
using FrameLedger.Infrastructure.Telemetry;

namespace FrameLedger.CaptureHost.Capture;

/// <summary>
/// The recorder as this host composes it (P2 PR-D): every adapter is the shipped one, over the host's OWN
/// ledger and its own <c>tmp\</c> beside the binary (decision D5). What the Agent's composition root will
/// do with DI, done here by hand — and a lower discard threshold, because an operator's bounded
/// <c>--seconds</c> capture is shorter than a session the Agent would keep.
/// </summary>
internal static class HostRecorder
{
    /// <summary>The host records bounded operator captures; the Agent keeps <see cref="SessionFinalizer.MinimumSessionLength"/>.</summary>
    public static readonly TimeSpan HostMinimumSessionLength = TimeSpan.FromSeconds(5);

    /// <summary>
    /// The host flushes its <c>.partial</c> every 3 s so a killed bounded capture still has a prefix worth
    /// recovering; the Agent keeps <see cref="PartialSessionWriter.DefaultFlushInterval"/> (60 s).
    /// </summary>
    public static readonly TimeSpan HostPartialFlushInterval = TimeSpan.FromSeconds(3);

    public static string PartialDirectory => Path.Combine(AppContext.BaseDirectory, "tmp");

    public static SessionRecorder Build(LedgerDatabase db, HostSessionFactory sessions) =>
        new(
            sessions,
            new SqliteGameRepository(db),
            new SqliteHardwareSnapshotRepository(db),
            new HardwareSnapshotSource(new DxgiAdapters()),
            new PartialSessionStore(PartialDirectory),
            Finalizer(db),
            new EventLogCrashSource(),
            Poller,
            TimeProvider.System,
            new RecorderOptions { MinimumSessionLength = HostMinimumSessionLength, PartialFlushInterval = HostPartialFlushInterval });

    public static SessionFinalizer Finalizer(LedgerDatabase db) => new(new SqliteSessionRepository(db), new DeflateSeriesCodec());

    public static PartialRecovery Recovery(LedgerDatabase db) => new(new PartialSessionStore(PartialDirectory), Finalizer(db), HostMinimumSessionLength);

    /// <summary>L1 + L2 + L3 under the composite, one poller per session; the poller owns and disposes the layers.</summary>
    private static TelemetryPoller Poller()
    {
        // Ownership walks up one step at a time and each local is nulled at its hand-off, so a throw
        // anywhere below disposes exactly what nobody owns yet.
        PdhAdapterMemoryCounter? pdh = null;
        BaselineTelemetrySource? l1 = null;
        LhmTelemetrySource? l2 = null;
        NvapiTelemetrySource? l3 = null;
        CompositeTelemetrySource? composite = null;
        try
        {
            pdh = new PdhAdapterMemoryCounter();
            l1 = new BaselineTelemetrySource(new DxgiAdapters(), pdh, TimeProvider.System);
            pdh = null;
            l2 = new LhmTelemetrySource(new LhmComputerAdapter(enableCpuAndMemory: false), new LhmTelemetryOptions(), TimeProvider.System);
            l3 = new NvapiTelemetrySource(TimeProvider.System);
            l1.Start();
            l2.Start();
            l3.Start();
            composite = new CompositeTelemetrySource([l1, l2, l3]);
            l1 = null;
            l2 = null;
            l3 = null;
            var poller = new TelemetryPoller(composite, new TelemetryPollerOptions(), TimeProvider.System, ownsSource: true);
            composite = null;
            return poller;
        }
        finally
        {
            pdh?.Dispose();
            l1?.Dispose();
            l2?.Dispose();
            l3?.Dispose();
            composite?.Dispose();
        }
    }
}
