using FrameLedger.Infrastructure.AntiCheat;
using FrameLedger.Infrastructure.Detection;

namespace FrameLedger.Infrastructure.Tests;

/// <summary>
/// §S21 — the guard and the managed reader must resolve the SAME file.
/// </summary>
/// <remarks>
/// <para>
/// <c>fl_ac_rules.h</c> calls the rules location "the ONE location … shared by the
/// injection guard and the Vulkan layer", and says a second reader pointing at a
/// different file "would be a second blocklist by accident". There were three
/// resolvers: the guard's, this assembly's, and the Vulkan probe's. Two of them
/// built the path from an inherited <c>LOCALAPPDATA</c>; this one asks the shell.
/// Nothing compared them.
/// </para>
/// <para>
/// That matters most for the thing §S20 is about to build. A seeder that writes
/// where the gate does not read succeeds, logs success, and leaves the guard
/// answering <c>RulesUnreadable</c> for every title — with a green test suite,
/// because the seeder and its test would agree with each other.
/// </para>
/// </remarks>
public sealed class RulesPathAgreementTests
{
    [Fact]
    public void TheNativeGuardAndTheManagedReaderResolveTheSameFile()
    {
        string native = NativeAntiCheatGuard.NativeRulesFilePath();
        string managed = DetectionRulesFile.DefaultPath;

        // Fail rather than skip if the native side could not answer. An empty
        // string compared against a real path would go red anyway, but saying so
        // explicitly keeps "the guard cannot resolve its own rules path" from
        // being reported as "the two disagree", which sends the next person to
        // the wrong file.
        Assert.False(
            string.IsNullOrEmpty(native),
            "FlGuardRulesFilePath returned nothing — the guard cannot resolve its own rules path, "
                + "which means it refuses every title on this machine.");

        // OrdinalIgnoreCase, not Ordinal: Windows paths are case-insensitive, and
        // the two APIs are free to differ on the casing of the profile directory.
        // A case-only difference is not the drift this test exists to catch, and
        // failing on it would train someone to weaken the assertion.
        Assert.Equal(managed, native, ignoreCase: true);
    }

    [Fact]
    public void TheResolvedPathIsAbsoluteAndNamesTheProductFile()
    {
        // Guards the shape independently of the comparison above: if BOTH sides
        // regressed to the same wrong answer — an empty Local AppData yielding a
        // relative path, say — the equality test would still pass.
        string native = NativeAntiCheatGuard.NativeRulesFilePath();

        Assert.True(Path.IsPathFullyQualified(native), $"not an absolute path: '{native}'");
        Assert.EndsWith(
            Path.Combine("FrameLedger", "rules", "detection-rules.json"),
            native,
            StringComparison.OrdinalIgnoreCase);
    }
}
