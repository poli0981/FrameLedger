namespace FrameLedger.Infrastructure.Telemetry;

/// <summary>
/// The bridge's C ABI as the managed sources see it, so <see cref="NvapiTelemetrySource"/> and
/// <see cref="NvapiNgxStateProbe"/> are tested on a machine with no NVIDIA driver. <see cref="NativeNvapiBridge"/>
/// is the real one.
/// </summary>
public interface INvapiBridge : IDisposable
{
    /// <summary><c>FlNvInit</c>: 0, <c>FL_NV_NO_GPU</c>, or the <c>NvAPI_Status</c> that refused — a normal condition.</summary>
    int Init();

    /// <summary><c>FlNvReadSample</c>: 0 with the sample filled, or a status.</summary>
    int ReadSample(out NvapiSample sample);

    /// <summary><c>FlNvNgxState</c>: 0 with the words filled (their <c>Status</c> says which branch), or a status.</summary>
    int NgxState(uint pid, out NvapiNgxWords words);

    /// <summary><c>FlNvDriverVersion</c>: 0 with the version (e.g. 61664) and branch, or a status.</summary>
    int DriverVersion(out uint version, out string branch);

    /// <summary><c>FlNvShutdown</c>.</summary>
    void Shutdown();
}
