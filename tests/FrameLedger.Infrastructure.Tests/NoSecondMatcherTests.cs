using System.Reflection;
using FluentAssertions;
using FrameLedger.Application.AntiCheat;
using FrameLedger.Domain.AntiCheat;

namespace FrameLedger.Infrastructure.Tests;

/// <summary>
/// Asserts that nothing managed matches an anti-cheat blocklist.
/// </summary>
/// <remarks>
/// <c>20_OPEN_QUESTIONS</c> §S15 item 1: two matchers that can disagree is a
/// fail-open by construction — the day they diverge, one of them is wrong and
/// nothing tells you which. The native guard owns all of it; the managed side
/// is a facade. This test is what keeps that true as the codebase grows.
/// </remarks>
public sealed class NoSecondMatcherTests
{
    private static readonly string[] _blocklistFamilies =
    [
        "EasyAntiCheat", "BEClient", "BEService", "vgk.sys", "denuvo",
        "GameGuard", "npgg", "xhunter", "PnkBstr", "mhyprot",
    ];

    [Fact]
    public void NoManagedTypeCarriesABlocklistToken()
    {
        // A literal blocklist token in managed code means somebody started
        // matching here. The single implementation lives in C++.
        Assembly[] assemblies =
        [
            typeof(IAntiCheatGuard).Assembly,                       // Application
            typeof(Domain.AntiCheat.AntiCheatVerdict).Assembly,     // Domain
            typeof(Infrastructure.AntiCheat.NativeAntiCheatGuard).Assembly,
        ];

        foreach (Assembly asm in assemblies)
        {
            foreach (Type type in asm.GetTypes())
            {
                foreach (FieldInfo field in type.GetFields(BindingFlags.Public | BindingFlags.NonPublic |
                                                           BindingFlags.Static | BindingFlags.Instance))
                {
                    if (!field.IsLiteral || field.FieldType != typeof(string))
                    {
                        continue;
                    }

                    string? value = field.GetRawConstantValue() as string;
                    if (value is null)
                    {
                        continue;
                    }

                    foreach (string token in _blocklistFamilies)
                    {
                        value.Should().NotContainEquivalentOf(token,
                            $"{type.FullName}.{field.Name} looks like a second blocklist matcher; " +
                            "the guard is native and there is exactly one (20_OPEN_QUESTIONS S15)");
                    }
                }
            }
        }
    }

    [Fact]
    public void TheGuardPortExposesNoEvidenceAndNoRules()
    {
        // A port that accepted evidence would let a caller decide what the
        // guard sees, which is the hole the native side closed by compiling the
        // seam out of everything that ships.
        MethodInfo[] methods = typeof(IAntiCheatGuard).GetMethods();

        // Raised from 2 to 3 as a reviewed act when the static pre-scan landed.
        //
        // The alternative was a second port, which would have kept this number
        // at 2 while the new surface grew somewhere this test never looks — the
        // count would have gone on passing precisely because the thing it
        // guards had moved. A number that has to be edited deliberately is the
        // point of it.
        methods.Should().HaveCount(3);

        // Stronger than the count, and it survives the port growing: every
        // parameter must be a primitive the caller cannot smuggle evidence
        // through, and every return must be a verdict.
        foreach (MethodInfo m in methods)
        {
            m.ReturnType.Should().Be<ValueTask<AntiCheatVerdict>>(
                $"{m.Name} must answer with a verdict and nothing else");

            foreach (ParameterInfo p in m.GetParameters())
            {
                p.ParameterType.Should().Match(t =>
                        t == typeof(int) || t == typeof(string) || t == typeof(CancellationToken),
                    $"{m.Name}.{p.Name} is a {p.ParameterType.Name}; the guard collects its own evidence");

                p.ParameterType.Name.Should().NotContainEquivalentOf("rule");
                p.ParameterType.Name.Should().NotContainEquivalentOf("source");
                p.ParameterType.Name.Should().NotContainEquivalentOf("evidence");
            }
        }
    }
}
