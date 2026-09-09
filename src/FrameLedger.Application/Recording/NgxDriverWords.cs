namespace FrameLedger.Application.Recording;

/// <summary>What <c>sessions.ngx_driver_words</c> stores: the driver's raw words as probed, plus how the probing went.</summary>
public sealed record NgxDriverWords
{
    public required string Outcome { get; init; }

    public string? Sr { get; init; }

    public string? Rr { get; init; }

    public string? Fg { get; init; }

    public uint? Driver { get; init; }

    public int Readings { get; init; }

    public int Answered { get; init; }

    public bool Changed { get; init; }

    public string? Detail { get; init; }
}
