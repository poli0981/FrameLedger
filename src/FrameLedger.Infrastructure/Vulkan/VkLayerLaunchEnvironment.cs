using System.Text;
using System.Text.Json;

namespace FrameLedger.Infrastructure.Vulkan;

/// <summary>
/// What a launched process needs for the Vulkan implicit layer to observe it, and nothing else on the machine to:
/// the loader's <c>VK_ADD_IMPLICIT_LAYER_PATH</c> pointed at a directory holding our manifest, the manifest's
/// <c>enable_environment</c> variable set, and the process image name on the layer's enable-list.
/// </summary>
/// <remarks>
/// <para>
/// <b>No registry.</b> <c>12_BUILD</c> §The Vulkan layer is not registered at install time makes registration a
/// deliberate, later act of the Agent, kept only while a Vulkan game has hooking enabled. The unshipped host
/// does not need it at all: loader 1.4.357 honours <c>VK_ADD_IMPLICIT_LAYER_PATH</c> (measured 2026-09-06,
/// <c>spike-notes</c> §2), so the layer is discoverable ONLY by the process this host starts — a smaller blast
/// radius than an HKCU registration, and gone when the variable is. The Agent's registration path (P2) is
/// unchanged by this.
/// </para>
/// <para>
/// <b>The enable-list is written here because this host stands in for the Agent</b>, which
/// <c>17_HOOK_ENGINE</c> §The enable-list names as its sole writer. One line, the lowercased image name, added
/// only when absent and removed on dispose only when this object added it. An entry somebody else wrote is
/// left exactly as found.
/// </para>
/// <para>
/// <b>Without a staged layer DLL nothing is written and no variable is set</b>: a manifest pointing at a file
/// that is not there would make the loader log an error into the game for no benefit.
/// </para>
/// </remarks>
public sealed class VkLayerLaunchEnvironment : IDisposable
{
    /// <summary>The manifest's <c>enable_environment</c> variable (<c>17_HOOK_ENGINE</c> §Vulkan).</summary>
    public const string EnableVariable = "FRAMELEDGER_ENABLE_VK_LAYER";

    /// <summary>The loader's additional implicit-layer search path, honoured without any registration.</summary>
    public const string ImplicitLayerPathVariable = "VK_ADD_IMPLICIT_LAYER_PATH";

    /// <summary>The layer's name, as the manifest and the loader log carry it.</summary>
    public const string LayerName = "VK_LAYER_FRAMELEDGER_overlay";

    private const string _manifestFileName = "VkLayer_FRAMELEDGER_overlay.json";

    private static readonly JsonSerializerOptions _manifestOptions = new() { WriteIndented = true };

    private readonly bool _addedEnableListEntry;

    private readonly string? _writtenManifest;

    private bool _disposed;

    private VkLayerLaunchEnvironment(string directory, string imageName, string enableListPath, string? manifest,
        bool addedEntry, IReadOnlyDictionary<string, string> variables)
    {
        Directory = directory;
        ImageName = imageName;
        EnableListPath = enableListPath;
        _writtenManifest = manifest;
        _addedEnableListEntry = addedEntry;
        Variables = variables;
    }

    /// <summary>The product's per-user layer directory: <c>%LOCALAPPDATA%\FrameLedger\vklayer</c>.</summary>
    public static string DefaultDirectory => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "FrameLedger", "vklayer");

    /// <summary>The directory the loader is pointed at, which also holds the enable-list.</summary>
    public string Directory { get; }

    /// <summary>The lowercased process image name written to the enable-list.</summary>
    public string ImageName { get; }

    /// <summary>The enable-list this object wrote to (or would have).</summary>
    public string EnableListPath { get; }

    /// <summary>The manifest written for this launch, or null when the layer DLL was not staged.</summary>
    public string? ManifestPath => _writtenManifest;

    /// <summary>Variables to add to the launched process's environment; empty when the layer is not staged.</summary>
    public IReadOnlyDictionary<string, string> Variables { get; }

    /// <summary>
    /// Prepare the environment for launching <paramref name="exePath"/> with the layer at
    /// <paramref name="layerDllPath"/>; <paramref name="directory"/> defaults to <see cref="DefaultDirectory"/>.
    /// </summary>
    public static VkLayerLaunchEnvironment Prepare(string exePath, string layerDllPath, string? directory = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(exePath);
        ArgumentException.ThrowIfNullOrWhiteSpace(layerDllPath);

        string dir = directory ?? DefaultDirectory;
#pragma warning disable CA1308 // the enable-list's format is LOWERCASED image names (17_HOOK_ENGINE §The enable-list)
        string image = Path.GetFileName(exePath).ToLowerInvariant();
#pragma warning restore CA1308
        string enableList = Path.Combine(dir, "enabled.txt");

        if (!File.Exists(layerDllPath))
        {
            return new VkLayerLaunchEnvironment(dir, image, enableList, manifest: null, addedEntry: false,
                new Dictionary<string, string>(StringComparer.Ordinal));
        }

        System.IO.Directory.CreateDirectory(dir);
        bool added = AddEnableListEntry(enableList, image);
        string manifest = Path.Combine(dir, _manifestFileName);
        File.WriteAllText(manifest, ManifestJson(Path.GetFullPath(layerDllPath)), new UTF8Encoding(false));

        var variables = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            [EnableVariable] = "1",
            [ImplicitLayerPathVariable] = dir,
        };
        return new VkLayerLaunchEnvironment(dir, image, enableList, manifest, added, variables);
    }

    /// <summary>The manifest, with the DLL's absolute path — the shape <c>VkLayer_FRAMELEDGER_overlay.json.in</c> generates.</summary>
    internal static string ManifestJson(string libraryPath)
    {
        var doc = new
        {
            file_format_version = "1.2.0",
            layer = new
            {
                name = LayerName,
                type = "GLOBAL",
                library_path = libraryPath,
                api_version = "1.4.357",
                implementation_version = "1",
                description = "FrameLedger performance capture (opt-in per game; passthrough otherwise)",
                enable_environment = new Dictionary<string, string>(StringComparer.Ordinal) { [EnableVariable] = "1" },
                disable_environment = new Dictionary<string, string>(StringComparer.Ordinal) { ["DISABLE_FRAMELEDGER_VK_LAYER"] = "1" },
            },
        };
        return JsonSerializer.Serialize(doc, _manifestOptions);
    }

    /// <summary>True when the entry was added; false when it was already there.</summary>
    private static bool AddEnableListEntry(string enableList, string image)
    {
        List<string> lines = File.Exists(enableList) ? [.. File.ReadAllLines(enableList)] : [];
        if (lines.Any(l => string.Equals(l.Trim(), image, StringComparison.OrdinalIgnoreCase)))
        {
            return false;
        }

        lines.Add(image);
        File.WriteAllText(enableList, string.Join('\n', lines) + '\n', new UTF8Encoding(false));
        return true;
    }

    /// <summary>Remove exactly what <see cref="Prepare"/> added.</summary>
    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        try
        {
            if (_addedEnableListEntry && File.Exists(EnableListPath))
            {
                List<string> kept = [.. File.ReadAllLines(EnableListPath)
                    .Where(l => !string.Equals(l.Trim(), ImageName, StringComparison.OrdinalIgnoreCase))];
                if (kept.Count == 0)
                {
                    File.Delete(EnableListPath);
                }
                else
                {
                    File.WriteAllText(EnableListPath, string.Join('\n', kept) + '\n', new UTF8Encoding(false));
                }
            }

            if (_writtenManifest is not null && File.Exists(_writtenManifest))
            {
                File.Delete(_writtenManifest);
            }
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            // Leaving a line or a manifest behind grants nothing: the layer still gates on the enable
            // variable, which only a launch sets. Stated rather than thrown from a Dispose.
        }
    }
}
