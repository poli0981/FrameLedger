using System.Diagnostics.CodeAnalysis;
using FrameLedger.Application.Telemetry;

namespace FrameLedger.Infrastructure.Telemetry;

/// <summary>
/// The layers merged with fixed per-field precedence, <b>L3 &gt; L2 &gt; L1</b>
/// (<c>docs/18_GPU_VENDOR_APIS.md</c> §Abstraction), recording which layer supplied each value.
/// </summary>
/// <remarks>
/// <para>
/// <b>Not a layer itself.</b> <see cref="Layer"/> is <see cref="TelemetryLayer.None"/>; the
/// composite's identity is <see cref="Descriptor"/> — the layers still standing, lowest first
/// (<c>l1+lhm</c>), which is what <c>sessions.telemetry_source</c> stores so a reader can see why
/// a field is missing. A merged sample's <see cref="GpuSample.Layer"/> is the highest layer that
/// contributed anything to it; <see cref="LayerOf"/> answers per field.
/// </para>
/// <para>
/// <b>A layer's fault policy is its own</b> — each disables itself after two faults and reports
/// <see cref="IGpuTelemetrySource.IsDisabled"/>. What the composite adds is the same rule one
/// level up: a layer whose <c>TryRead</c> throws — which the port says it must not — is counted
/// here, and the second time it is excluded for the session, from reads and from the
/// descriptor alike. The other layers are unaffected either way; that is the point of
/// composing rather than competing.
/// </para>
/// <para>
/// Owns its layers: disposing the composite disposes them.
/// </para>
/// </remarks>
public sealed class CompositeTelemetrySource : IGpuTelemetrySource
{
    /// <summary>Throws tolerated from one layer's <c>TryRead</c> before it is excluded. The second one excludes.</summary>
    public const int MaxFaults = 2;

    private const int _fieldBits = 11;

    private readonly IGpuTelemetrySource[] _layers;
    private readonly int[] _faults;
    private readonly string?[] _lastFault;
    private readonly TelemetryLayer[] _fieldLayers = new TelemetryLayer[_fieldBits];
    private readonly Lock _lock = new();
    private bool _disposed;

    public CompositeTelemetrySource(IEnumerable<IGpuTelemetrySource> layers)
    {
        ArgumentNullException.ThrowIfNull(layers);
        _layers = [.. layers.OrderByDescending(l => l.Layer)];
        if (_layers.Any(l => l.Layer == TelemetryLayer.None))
        {
            throw new ArgumentException("a layer must say which layer it is", nameof(layers));
        }

        if (_layers.Select(l => l.Layer).Distinct().Count() != _layers.Length)
        {
            throw new ArgumentException("one source per layer; two claiming the same layer would compete, not compose", nameof(layers));
        }

        _faults = new int[_layers.Length];
        _lastFault = new string?[_layers.Length];
    }

    /// <summary>Always <see cref="TelemetryLayer.None"/>: see the class remarks.</summary>
    public TelemetryLayer Layer => TelemetryLayer.None;

    public GpuCapabilities Capabilities
    {
        get
        {
            GpuCapabilities all = GpuCapabilities.None;
            for (int i = 0; i < _layers.Length; i++)
            {
                if (IsStanding(i))
                {
                    all |= _layers[i].Capabilities;
                }
            }

            return all;
        }
    }

    /// <summary>True when no layer is standing.</summary>
    public bool IsDisabled => !Enumerable.Range(0, _layers.Length).Any(IsStanding);

    /// <summary>The layers still standing, lowest first: <c>l1+lhm+nvapi</c>. Empty when none is.</summary>
    public string Descriptor =>
        TelemetryLayerNames.Describe(Enumerable.Range(0, _layers.Length).Where(IsStanding).Select(i => _layers[i].Layer));

    /// <summary>What a layer's last throw said, for the report. Null if it never threw.</summary>
    public string? LastFaultOf(TelemetryLayer layer)
    {
        int i = Array.FindIndex(_layers, l => l.Layer == layer);
        return i < 0 ? null : Volatile.Read(ref _lastFault[i]);
    }

    /// <summary>
    /// Which layer supplied <paramref name="field"/> in the most recent merged sample;
    /// <see cref="TelemetryLayer.None"/> if nothing did. Exactly one bit.
    /// </summary>
    public TelemetryLayer LayerOf(GpuCapabilities field)
    {
        int bit = BitOf(field);
        lock (_lock)
        {
            return _fieldLayers[bit];
        }
    }

    public bool TryRead([NotNullWhen(true)] out GpuSample? sample)
    {
        sample = null;
        lock (_lock)
        {
            Array.Clear(_fieldLayers);
            for (int i = 0; i < _layers.Length; i++)
            {
                if (!IsStanding(i) || !ReadLayer(i, out GpuSample? layerSample))
                {
                    continue;
                }

                GpuCapabilities added = layerSample.PresentFields & ~(sample?.PresentFields ?? GpuCapabilities.None);
                Attribute(added, _layers[i].Layer);
                sample = sample is null ? layerSample : Merge(sample, layerSample);
            }
        }

        return sample is not null;
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        foreach (IGpuTelemetrySource layer in _layers)
        {
            layer.Dispose();
        }
    }

    private static int BitOf(GpuCapabilities field)
    {
        int value = (int)field;
        if (value == 0 || (value & (value - 1)) != 0 || int.TrailingZeroCount(value) >= _fieldBits)
        {
            throw new ArgumentOutOfRangeException(nameof(field), field, "exactly one capability bit");
        }

        return int.TrailingZeroCount(value);
    }

    /// <summary>Every field the upper sample lacks, taken from the lower one. Identity stays the upper's.</summary>
    private static GpuSample Merge(GpuSample upper, GpuSample lower) => upper with
    {
        AdapterName = upper.AdapterName ?? lower.AdapterName,
        TempCoreC = upper.TempCoreC ?? lower.TempCoreC,
        TempHotspotC = upper.TempHotspotC ?? lower.TempHotspotC,
        TempMemoryC = upper.TempMemoryC ?? lower.TempMemoryC,
        LoadPct = upper.LoadPct ?? lower.LoadPct,
        VramAdapterMb = upper.VramAdapterMb ?? lower.VramAdapterMb,
        CoreClockMhz = upper.CoreClockMhz ?? lower.CoreClockMhz,
        MemClockMhz = upper.MemClockMhz ?? lower.MemClockMhz,
        PowerW = upper.PowerW ?? lower.PowerW,
        FanRpm = upper.FanRpm ?? lower.FanRpm,
        ThrottleReasons = upper.ThrottleReasons ?? lower.ThrottleReasons,
        PcieGen = upper.PcieGen ?? lower.PcieGen,
        PcieWidth = upper.PcieWidth ?? lower.PcieWidth,
    };

    private void Attribute(GpuCapabilities added, TelemetryLayer layer)
    {
        for (int bit = 0; bit < _fieldBits; bit++)
        {
            if ((added & (GpuCapabilities)(1 << bit)) != GpuCapabilities.None)
            {
                _fieldLayers[bit] = layer;
            }
        }
    }

    private bool IsStanding(int i) => Volatile.Read(ref _faults[i]) < MaxFaults && !_layers[i].IsDisabled;

    private bool ReadLayer(int i, [NotNullWhen(true)] out GpuSample? sample)
    {
        try
        {
            return _layers[i].TryRead(out sample);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            Volatile.Write(ref _lastFault[i], $"TryRead: {ex.GetType().Name}: {ex.Message}");
            Interlocked.Increment(ref _faults[i]);
            sample = null;
            return false;
        }
    }
}
