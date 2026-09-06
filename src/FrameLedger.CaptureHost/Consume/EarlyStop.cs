using System.Globalization;
using System.Text.Json;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Consume;

/// <summary>
/// The Overlay's <c>LoadLibrary</c> detour, as the report reads it: whether the detour is in, how often it woke
/// the watchdog for a module the inventory or census names, and — when the Overlay stopped itself — which
/// anti-cheat family the module that arrived matched (<c>fl_shm.h</c> §loaderSignals / earlyStopFamily,
/// <c>19_SAFETY</c> §During a session, the in-process half; §S6).
/// </summary>
internal static class EarlyStop
{
    private const ushort _installedBit = 0x8000;

    private const ushort _countMask = 0x7FFF;

    /// <summary>The seed rules file staged beside this host (<c>FrameLedger.Rules.targets</c>).</summary>
    public static string StagedRulesPath => Path.Combine(AppContext.BaseDirectory, "rules", "detection-rules.json");

    /// <summary>
    /// The family name behind a 1-based index into <c>anticheat.modules</c> of the rules file — the same order
    /// <c>gen-ac-floor.ps1</c> emits the compiled floor's MODULE families in, which is what the Overlay indexes.
    /// Null when the file cannot be read or the index is outside it; never a guess.
    /// </summary>
    public static string? FamilyName(int index, string rulesPath)
    {
        if (index <= 0)
        {
            return null;
        }

        try
        {
            using JsonDocument doc = JsonDocument.Parse(File.ReadAllBytes(rulesPath));
            if (!doc.RootElement.TryGetProperty("anticheat", out JsonElement anticheat)
                || !anticheat.TryGetProperty("modules", out JsonElement modules)
                || modules.ValueKind != JsonValueKind.Array)
            {
                return null;
            }

            int i = 0;
            foreach (JsonElement family in modules.EnumerateArray())
            {
                if (++i == index)
                {
                    return family.TryGetProperty("family", out JsonElement name) ? name.GetString() : null;
                }
            }

            return null;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException)
        {
            return null;
        }
    }

    /// <summary>The report's two lines: the detour's state always, the stop only when it happened.</summary>
    public static IEnumerable<string> Describe(FlWriterState writer, string rulesPath)
    {
        bool installed = (writer.LoaderSignals & _installedBit) != 0;
        int wakes = writer.LoaderSignals & _countMask;
        yield return installed
            ? $"  loader detour: installed (kernelbase!LoadLibraryExW); woke the watchdog {wakes.ToString(CultureInfo.InvariantCulture)} time(s) for a module the inventory or census names"
            : "  loader detour: NOT installed - late vendor modules wait for the 1 Hz tick, and the in-process anti-cheat stop is absent; the host's 30 s scan is the only stop";

        if (writer.EarlyStopFamily == 0)
        {
            yield break;
        }

        string? name = FamilyName(writer.EarlyStopFamily, rulesPath);
        yield return "  EARLY STOP: the Overlay stopped ITSELF - a module matching anti-cheat family #"
                     + writer.EarlyStopFamily.ToString(CultureInfo.InvariantCulture)
                     + (name is null ? " (name not resolvable from the staged rules file)" : $" ({name})")
                     + " loaded mid-session (19_SAFETY §During a session, the in-process half); status="
                     + ((FlStatus)writer.Status).ToString()
                     + ". This is the exact-name floor only; the fragment-and-signer tier is the host's scan";
    }
}
