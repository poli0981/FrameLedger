using FluentAssertions;
using FrameLedger.Infrastructure.Detection;

namespace FrameLedger.Infrastructure.Tests.Detection;

/// <summary>
/// Where a game's install begins, given its executable.
/// </summary>
/// <remarks>
/// Every case here is a real layout measured on 2026-08-03, not an invented one.
/// The Unreal case is the one that mattered: the executable sits three levels
/// below everything that identifies the game, so the first version of the probe
/// reported Lies of P as shipping no DLSS and — far worse — the anti-cheat
/// pre-scan would have looked for `EasyAntiCheat/` in a folder that holds the
/// shipping binary and nothing else.
/// </remarks>
public sealed class InstallRootTests
{
    [Theory]
    // Unreal: exe three levels below the install root.
    [InlineData(@"D:\SteamLibrary\steamapps\common\Lies of P\LiesofP\Binaries\Win64\LOP-Win64-Shipping.exe",
        @"D:\SteamLibrary\steamapps\common\Lies of P")]
    // Unity: exe already at the root.
    [InlineData(@"D:\SteamLibrary\steamapps\common\Deadly Heart Gambit\DeadlyHeartGambit.exe",
        @"D:\SteamLibrary\steamapps\common\Deadly Heart Gambit")]
    // Whole-segment, case-insensitive.
    [InlineData(@"C:\Games\STEAMAPPS\Common\Title\Bin\game.exe", @"C:\Games\STEAMAPPS\Common\Title")]
    [InlineData(@"C:\Program Files\Epic Games\SomeTitle\Sub\game.exe", @"C:\Program Files\Epic Games\SomeTitle")]
    [InlineData(@"D:\GOG Galaxy\Games\SomeTitle\bin\game.exe", @"D:\GOG Galaxy\Games\SomeTitle")]
    public void RecognisedLayoutsResolveToTheInstallRoot(string exe, string expected) =>
        InstallRoot.Resolve(exe).Should().Be(expected);

    [Theory]
    // Alan Wake 2 as actually installed: no store boundary anywhere in the path.
    [InlineData(@"D:\another\epic\AlanWake2\AlanWake2.exe", @"D:\another\epic\AlanWake2")]
    // A trailing `steamapps` is not a boundary — a boundary needs a child.
    [InlineData(@"D:\backup\steamapps\stray.exe", @"D:\backup\steamapps")]
    public void AnUnrecognisedLayoutKeepsTheExeDirectory(string exe, string expected)
    {
        // Walking up blindly would be worse than staying put: one level above
        // D:\another\epic\AlanWake2 is a folder of unrelated games, and refusing
        // this title because a sibling ships anti-cheat is a false refusal with
        // no appeal.
        InstallRoot.Resolve(exe).Should().Be(expected);
    }

    [Fact]
    public void BothExecutablesOfAnUnrealTitleResolveToTheSameRoot()
    {
        // Unreal titles conventionally ship TWO executables: a shim at the
        // install root and the real shipping binary nested under
        // <Project>\Binaries\Win64\. Lies of P has LOP.exe and
        // LOP-Win64-Shipping.exe; measured 2026-08-03.
        //
        // Which one a user adds to their watchlist must not change what we
        // detect, or what the anti-cheat pre-scan looks at. Before this
        // resolution existed the two answered differently — the root shim gave
        // "everything undetermined", the nested binary gave "fsr only" — and
        // neither was right.
        const string root = @"D:\SteamLibrary\steamapps\common\Lies of P";

        string viaShim = InstallRoot.Resolve(Path.Combine(root, "LOP.exe"));
        string viaShipping = InstallRoot.Resolve(
            Path.Combine(root, @"LiesofP\Binaries\Win64\LOP-Win64-Shipping.exe"));

        viaShim.Should().Be(root);
        viaShipping.Should().Be(root);
        viaShim.Should().Be(viaShipping, "the entry point must not change the answer");
    }

    [Fact]
    public void ItRejectsAnEmptyPath() =>
        ((Action)(() => InstallRoot.Resolve("  "))).Should().Throw<ArgumentException>();
}
