namespace FrameLedger.Domain.Detection;

/// <summary>How the signals in a group combine.</summary>
public enum SignalCombinator
{
    /// <summary>Every signal must match.</summary>
    All,

    /// <summary>At least one signal must match.</summary>
    Any,
}
