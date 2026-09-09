namespace FrameLedger.Application.Telemetry;

/// <summary>
/// What DXGI says an adapter is (<c>18_GPU_VENDOR_APIS</c> §L1). Read once per session; the
/// hardware snapshot (<c>06_DATA_MODEL</c>) is built from it.
/// </summary>
/// <remarks>
/// <para>
/// <see cref="Luid"/> is the same 64-bit value the Overlay publishes in the handshake
/// (<c>FlShmHandshake.adapterLuid</c>, low part in the low 32 bits), so the Agent can pick the
/// adapter the game actually presented on rather than the one DXGI lists first. On a
/// single-adapter machine the two are the same adapter and the match is trivial;
/// multi-adapter filtering is written and can only be reasoned about here
/// (<c>18_GPU_VENDOR_APIS</c> §Capability matrix).
/// </para>
/// <para>
/// <see cref="DriverVersion"/> is the user-mode driver version DXGI reports through
/// <c>IDXGIAdapter::CheckInterfaceSupport</c>, in the four-part form the vendor's own tooling
/// shows (<c>32.0.15.6156</c>); it is not the marketing number (<c>561.09</c>), which only L3
/// knows. Null when the query failed — never a guess.
/// </para>
/// </remarks>
public sealed record GpuAdapterIdentity
{
    public required string Name { get; init; }

    /// <summary>Packed LUID: <c>HighPart &lt;&lt; 32 | LowPart</c>.</summary>
    public required ulong Luid { get; init; }

    public required uint VendorId { get; init; }

    public required uint DeviceId { get; init; }

    public required uint SubSysId { get; init; }

    public required uint Revision { get; init; }

    /// <summary>Adapter-local memory, MiB. What the vendor sold, not what is in use.</summary>
    public required double DedicatedVideoMemoryMb { get; init; }

    public required double SharedSystemMemoryMb { get; init; }

    /// <summary>A software (WARP / reference) adapter. Never selected by default.</summary>
    public required bool IsSoftware { get; init; }

    public string? DriverVersion { get; init; }
}
