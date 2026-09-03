using LibreHardwareMonitor.Hardware;

namespace FrameLedger.Infrastructure.Telemetry;

/// <summary>
/// <see cref="ILhmComputer"/> over the real <see cref="Computer"/>.
/// </summary>
/// <remarks>
/// <para>
/// <c>IsGpuEnabled</c> is always on; <c>IsCpuEnabled</c> and <c>IsMemoryEnabled</c> only when
/// the caller says so, and the caller may only say so when the Agent is elevated and PawnIO
/// is present (<c>18_GPU_VENDOR_APIS</c> §L2). Nothing else is ever enabled — motherboard,
/// storage, network, battery, PSU and controllers are outside what this product measures,
/// and every group enabled is a driver path that can fault.
/// </para>
/// <para>
/// Consumed <b>unmodified</b>, as an ordinary NuGet package reference. MPL-2.0 §3.3 is what
/// makes that combinable with GPLv3; modifying a file of it would change the obligation
/// (<c>legal/THIRD_PARTY_NOTICES.md</c> §GPL-3.0 compatibility).
/// </para>
/// </remarks>
public sealed class LhmComputerAdapter : ILhmComputer
{
    private readonly Computer _computer;

    public LhmComputerAdapter(bool enableCpuAndMemory)
    {
        _computer = new Computer
        {
            IsGpuEnabled = true,
            IsCpuEnabled = enableCpuAndMemory,
            IsMemoryEnabled = enableCpuAndMemory,
        };
    }

    public IReadOnlyList<IHardware> Hardware => [.. _computer.Hardware];

    public void Open() => _computer.Open();

    public void Update() => _computer.Accept(new UpdateVisitor());

    public void Close() => _computer.Close();
}
