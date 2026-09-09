namespace FrameLedger.Application.Capture;

/// <summary>How the host finds the process to capture. There is no pid argument anywhere.</summary>
public interface ITargetResolver
{
    /// <summary>
    /// The single running process whose image is <paramref name="normalisedExePath"/>.
    /// </summary>
    /// <returns>The pid, or null with <paramref name="reason"/> set.</returns>
    int? Resolve(string normalisedExePath, out SessionEndReason reason);
}
