namespace FrameLedger.Application.Recording;

/// <summary>What <see cref="CrashAutoDisablePolicy.ApplyAsync"/> did.</summary>
public enum CrashPolicyOutcome
{
    NotAnEarlyCrash = 0,

    /// <summary>An early crash, counted; hooking stays on.</summary>
    Counted,

    /// <summary>The second early crash: hooking is now off for this game, with the reason on the row.</summary>
    HookingDisabled,
}
