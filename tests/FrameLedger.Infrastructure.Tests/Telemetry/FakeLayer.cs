using System.Diagnostics.CodeAnalysis;
using FrameLedger.Application.Telemetry;

namespace FrameLedger.Infrastructure.Tests.Telemetry;

/// <summary>A scripted <see cref="IGpuTelemetrySource"/>: whatever sample it is told to publish next.</summary>
internal sealed class FakeLayer : IGpuTelemetrySource
{
    private GpuSample? _next;
    private Func<GpuSample?>? _script;

    public FakeLayer(TelemetryLayer layer) => Layer = layer;

    public TelemetryLayer Layer { get; }

    public GpuCapabilities Capabilities { get; set; }

    public bool IsDisabled { get; set; }

    public int Reads { get; private set; }

    public bool Disposed { get; private set; }

    public FakeLayer Publish(GpuSample? sample)
    {
        _next = sample;
        _script = null;
        if (sample is not null)
        {
            Capabilities |= sample.PresentFields;
        }

        return this;
    }

    public FakeLayer Script(Func<GpuSample?> script)
    {
        _script = script;
        return this;
    }

    public GpuSample Sample(DateTimeOffset takenAt, double? load = null, double? vram = null, double? temp = null,
        double? power = null, string? adapter = null) => new()
        {
            TakenAt = takenAt,
            Layer = Layer,
            AdapterName = adapter,
            LoadPct = load,
            VramAdapterMb = vram,
            TempCoreC = temp,
            PowerW = power,
        };

    public bool TryRead([NotNullWhen(true)] out GpuSample? sample)
    {
        Reads++;
        sample = _script is null ? _next : _script();
        return sample is not null;
    }

    public void Dispose() => Disposed = true;
}
