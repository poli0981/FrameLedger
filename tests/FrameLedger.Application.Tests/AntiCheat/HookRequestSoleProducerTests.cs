using System.Reflection;
using FluentAssertions;
using FrameLedger.Application.AntiCheat;

namespace FrameLedger.Application.Tests.AntiCheat;

/// <summary>
/// <c>20_OPEN_QUESTIONS</c> §S27: the request the gate evaluates has exactly one producer,
/// <see cref="HookRequest.FromConsent"/>. Now that the session loop ships in Application (P2 PR-C),
/// the shape is pinned by reflection rather than remembered — a public constructor, an <c>init</c>
/// setter or a second factory would each be a place to synthesise consent nobody gave.
/// </summary>
public sealed class HookRequestSoleProducerTests
{
    [Fact]
    public void HookRequestHasNoPublicConstructorNoSetterAndExactlyOneFactory()
    {
        Type t = typeof(HookRequest);

        t.IsSealed.Should().BeTrue();
        t.GetConstructors(BindingFlags.Public | BindingFlags.Instance).Should().BeEmpty("the rejected synthesis must not compile");
        t.GetProperties(BindingFlags.Public | BindingFlags.Instance)
            .Where(p => p.SetMethod is not null)
            .Should().BeEmpty("get-only: an init setter is the record shape §S27 rejected");

        MethodInfo[] factories = t.GetMethods(BindingFlags.Public | BindingFlags.Static)
            .Where(m => m.ReturnType == t)
            .ToArray();
        factories.Select(m => m.Name).Should().Equal(["FromConsent"], "one factory, named for what it takes");
    }
}
