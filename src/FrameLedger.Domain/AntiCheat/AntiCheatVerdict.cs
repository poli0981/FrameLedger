namespace FrameLedger.Domain.AntiCheat;

/// <summary>
/// The guard's answer: whether injection may proceed, and if not, what fired.
/// </summary>
/// <remarks>
/// Deliberately NOT a clearance token. <c>20_OPEN_QUESTIONS</c> §S8/§S13(b):
/// a token that escapes the guard can simply be ignored by a caller who never
/// asks for one, so the guard owns the chokepoint and performs the injection
/// itself. This type is a REPORT of something that already happened — it
/// authorises nothing, and holding one grants no ability to inject.
/// </remarks>
public readonly record struct AntiCheatVerdict
{
    /// <summary>
    /// False on a default-constructed value, and that is the entire reason it
    /// exists.
    /// </summary>
    /// <remarks>
    /// The native side gives <c>Verdict</c> a member initialiser so the default
    /// is a refusal. C# cannot: every field of a <c>struct</c> zeroes, and
    /// <see cref="AntiCheatRefusalReason.Allow"/> is 0 — which it must stay,
    /// because it mirrors <c>fl::guard::Reason</c>. So instead of bending the
    /// mirror, the verdict records whether it came from an evaluation at all.
    /// A value nobody assigned has not evaluated anything and cannot permit
    /// anything.
    /// </remarks>
    private readonly bool _evaluated;

    private AntiCheatVerdict(AntiCheatRefusalReason reason, string family, string signal)
    {
        Reason = reason;
        Family = family;
        Signal = signal;
        _evaluated = true;
    }

    /// <summary>Why the guard refused, or <see cref="AntiCheatRefusalReason.Allow"/>.</summary>
    public AntiCheatRefusalReason Reason { get; }

    /// <summary>The anti-cheat family that fired, e.g. <c>Easy Anti-Cheat</c>. Empty when allowed.</summary>
    public string Family { get; }

    /// <summary>The specific signal, e.g. <c>EasyAntiCheat_EOS.dll</c>. Empty when allowed.</summary>
    public string Signal { get; }

    /// <summary>
    /// True only when a real evaluation happened AND every check passed.
    /// A default-constructed verdict is never allowed.
    /// </summary>
    public bool IsAllowed => _evaluated && Reason == AntiCheatRefusalReason.Allow;

    /// <summary>
    /// A refusal. The default-constructed value is deliberately a refusal too:
    /// if a code path ever forgets to assign one, what it leaves behind must
    /// not read as permission.
    /// </summary>
    public static AntiCheatVerdict Refused(AntiCheatRefusalReason reason, string family, string signal) =>
        reason == AntiCheatRefusalReason.Allow
            ? throw new ArgumentOutOfRangeException(nameof(reason), "Refused() cannot carry Allow.")
            : new AntiCheatVerdict(reason, family, signal);

    /// <summary>Every check passed.</summary>
    public static AntiCheatVerdict Allowed() =>
        new(AntiCheatRefusalReason.Allow, string.Empty, string.Empty);

    /// <summary>
    /// Build from the native ABI's reason code. An unrecognised code refuses:
    /// it means the managed mirror has drifted from <c>fl::guard::Reason</c>,
    /// and a gate that does not understand its own answer must not allow.
    /// </summary>
    public static AntiCheatVerdict FromNative(int reason, string family, string signal)
    {
        if (reason == (int)AntiCheatRefusalReason.Allow)
        {
            return Allowed();
        }

        return Enum.IsDefined(typeof(AntiCheatRefusalReason), reason)
            ? new AntiCheatVerdict((AntiCheatRefusalReason)reason, family, signal)
            : new AntiCheatVerdict(AntiCheatRefusalReason.RulesIncomplete, family,
                $"unrecognised native reason {reason}; the managed mirror has drifted");
    }
}
