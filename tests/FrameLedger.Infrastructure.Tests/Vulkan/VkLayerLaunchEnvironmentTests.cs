using System.Text.Json;
using FluentAssertions;
using FrameLedger.Infrastructure.Vulkan;

namespace FrameLedger.Infrastructure.Tests.Vulkan;

public sealed class VkLayerLaunchEnvironmentTests : IDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "fl-vklayer-" + Guid.NewGuid().ToString("N"));

    private readonly string _dll;

    public VkLayerLaunchEnvironmentTests()
    {
        Directory.CreateDirectory(_dir);
        _dll = Path.Combine(_dir, "FrameLedger.VkLayer.dll");
        File.WriteAllBytes(_dll, [0x4D, 0x5A]);
    }

    public void Dispose()
    {
        try
        {
            Directory.Delete(_dir, recursive: true);
        }
        catch (IOException)
        {
        }
    }

    private string LayerDir => Path.Combine(_dir, "vklayer");

    [Fact]
    public void PrepareWritesTheManifestTheEnableListEntryAndTheTwoVariables()
    {
        using var env = VkLayerLaunchEnvironment.Prepare(@"D:\Games\Title\Game.exe", _dll, LayerDir);

        env.ImageName.Should().Be("game.exe", "the enable-list holds lowercased image names");
        env.Variables.Should().Contain(VkLayerLaunchEnvironment.EnableVariable, "1");
        env.Variables.Should().Contain(VkLayerLaunchEnvironment.ImplicitLayerPathVariable, LayerDir);
        File.ReadAllText(env.EnableListPath).Should().Contain("game.exe");

        env.ManifestPath.Should().NotBeNull();
        using JsonDocument manifest = JsonDocument.Parse(File.ReadAllText(env.ManifestPath!));
        JsonElement layer = manifest.RootElement.GetProperty("layer");
        layer.GetProperty("name").GetString().Should().Be(VkLayerLaunchEnvironment.LayerName);
        layer.GetProperty("library_path").GetString().Should().Be(Path.GetFullPath(_dll));
        layer.GetProperty("enable_environment").GetProperty(VkLayerLaunchEnvironment.EnableVariable).GetString()
            .Should().Be("1", "the loader compares the VALUE, so the manifest must say what the host sets");
    }

    [Fact]
    public void DisposeRemovesExactlyWhatPrepareAddedAndNothingElse()
    {
        Directory.CreateDirectory(LayerDir);
        string enableList = Path.Combine(LayerDir, "enabled.txt");
        File.WriteAllText(enableList, "# somebody's list\nother.exe\n");

        string manifest;
        using (var env = VkLayerLaunchEnvironment.Prepare(@"D:\Games\Title\Game.exe", _dll, LayerDir))
        {
            manifest = env.ManifestPath!;
            File.ReadAllLines(enableList).Should().Contain("game.exe").And.Contain("other.exe");
        }

        File.ReadAllLines(enableList).Should().NotContain("game.exe").And.Contain("other.exe").And.Contain("# somebody's list");
        File.Exists(manifest).Should().BeFalse();
    }

    [Fact]
    public void AnEntrySomebodyElseWroteIsNeitherDuplicatedNorRemoved()
    {
        Directory.CreateDirectory(LayerDir);
        string enableList = Path.Combine(LayerDir, "enabled.txt");
        File.WriteAllText(enableList, "GAME.EXE\n");

        using (VkLayerLaunchEnvironment.Prepare(@"D:\Games\Title\Game.exe", _dll, LayerDir))
        {
            File.ReadAllLines(enableList).Should().ContainSingle("the existing entry matches case-insensitively");
        }

        File.ReadAllLines(enableList).Should().ContainSingle().Which.Should().Be("GAME.EXE");
    }

    [Fact]
    public void WithoutAStagedLayerNothingIsWrittenAndNoVariableIsSet()
    {
        using var env = VkLayerLaunchEnvironment.Prepare(@"D:\Games\Title\Game.exe", Path.Combine(_dir, "missing.dll"), LayerDir);

        env.Variables.Should().BeEmpty("a manifest pointing at nothing would only make the loader log an error into the game");
        env.ManifestPath.Should().BeNull();
        Directory.Exists(LayerDir).Should().BeFalse();
    }
}
