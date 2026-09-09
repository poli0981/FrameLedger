namespace FrameLedger.Domain.Metrics;

/// <summary>Mirror of <c>FlApi</c>: the presentation API a sample came through.</summary>
public enum FrameApi
{
    Unknown = 0,
    D3D11 = 1,
    D3D12 = 2,
    Vulkan = 3,
    OpenGL = 4,
}
