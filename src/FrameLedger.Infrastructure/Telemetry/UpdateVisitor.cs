using LibreHardwareMonitor.Hardware;

namespace FrameLedger.Infrastructure.Telemetry;

/// <summary>
/// The visitor LibreHardwareMonitor's own sample uses to refresh a tree: update each
/// hardware node, then recurse into its sub-hardware. Sensors and parameters need nothing.
/// </summary>
internal sealed class UpdateVisitor : IVisitor
{
    public void VisitComputer(IComputer computer)
    {
        ArgumentNullException.ThrowIfNull(computer);
        computer.Traverse(this);
    }

    public void VisitHardware(IHardware hardware)
    {
        ArgumentNullException.ThrowIfNull(hardware);
        hardware.Update();
        foreach (IHardware sub in hardware.SubHardware)
        {
            sub.Accept(this);
        }
    }

    public void VisitSensor(ISensor sensor)
    {
        // Nothing to refresh: a sensor's value is written by its hardware's Update().
    }

    public void VisitParameter(IParameter parameter)
    {
        // Parameters are tuning inputs, and this project never writes one.
    }
}
