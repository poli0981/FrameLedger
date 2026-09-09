using System.Globalization;
using Windows.Win32;
using Windows.Win32.System.Performance;

namespace FrameLedger.Infrastructure.Telemetry;

/// <summary>
/// <c>\GPU Adapter Memory(luid_…_phys_0)\Dedicated Usage</c> through PDH — the same source Task
/// Manager's "Dedicated GPU memory" is, fully documented, every vendor
/// (<c>18_GPU_VENDOR_APIS</c> §L1).
/// </summary>
/// <remarks>
/// <para>
/// <b>Bound by instance name, never summed across a wildcard.</b> The instance embeds the LUID
/// as <c>luid_0x&lt;HighPart&gt;_0x&lt;LowPart&gt;_phys_&lt;n&gt;</c>, so the counter for the
/// adapter the game presents on is addressed directly and a second GPU never leaks into the
/// figure. <c>phys_0</c> is the first physical adapter under the LUID, which is the only one on a
/// single-GPU LUID; linked-adapter nodes are not addressed (nothing in the pipeline is
/// per-node, and the multi-adapter cell in the matrix is reasoned about, not measured).
/// </para>
/// <para>
/// <b>Absent is not a fault.</b> A machine without the GPU counter set (a VM, a runner) fails
/// <see cref="TryOpen"/> and the field is N/A with the layer standing. A read that fails after a
/// successful open throws, and the source counts it under the port's two-fault rule.
/// </para>
/// <para>
/// <b>Utilisation is deliberately not read here.</b> <c>20_OPEN_QUESTIONS</c> §M10: the engine
/// counters summed do not reproduce the figure the doc invoked, so <c>LoadPct</c> is L2's
/// vendor-reported load and is labelled as such.
/// </para>
/// </remarks>
public sealed class PdhAdapterMemoryCounter : IAdapterMemoryCounter
{
    private PDH_HQUERY _query;
    private PDH_HCOUNTER _counter;
    private bool _open;

    public bool TryOpen(ulong luid)
    {
        Close();
        if (PInvoke.PdhOpenQuery(null, 0, out PDH_HQUERY query) != 0)
        {
            return false;
        }

        string path = string.Create(CultureInfo.InvariantCulture,
            $@"\GPU Adapter Memory(luid_0x{(uint)(luid >> 32):X8}_0x{(uint)luid:X8}_phys_0)\Dedicated Usage");
        if (PInvoke.PdhAddEnglishCounter(query, path, 0, out PDH_HCOUNTER counter) != 0 || PInvoke.PdhCollectQueryData(query) != 0)
        {
            _ = PInvoke.PdhCloseQuery(query);
            return false;
        }

        _query = query;
        _counter = counter;
        _open = true;
        return true;
    }

    public ulong ReadDedicatedUsageBytes()
    {
        if (!_open)
        {
            throw new InvalidOperationException("no counter is open");
        }

        uint status = PInvoke.PdhCollectQueryData(_query);
        if (status != 0)
        {
            throw new InvalidOperationException(string.Create(CultureInfo.InvariantCulture, $"PdhCollectQueryData: 0x{status:X8}"));
        }

        status = PInvoke.PdhGetFormattedCounterValue(_counter, PDH_FMT.PDH_FMT_LARGE, out _, out PDH_FMT_COUNTERVALUE value);
        if (status != 0)
        {
            throw new InvalidOperationException(string.Create(CultureInfo.InvariantCulture, $"PdhGetFormattedCounterValue: 0x{status:X8}"));
        }

        return (ulong)value.Anonymous.largeValue;
    }

    public void Dispose() => Close();

    private void Close()
    {
        if (!_open)
        {
            return;
        }

        _open = false;
        _ = PInvoke.PdhCloseQuery(_query);
    }
}
