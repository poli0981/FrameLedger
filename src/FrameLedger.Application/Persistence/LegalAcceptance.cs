namespace FrameLedger.Application.Persistence;

/// <summary>One accepted legal document: which version, and when.</summary>
public sealed record LegalAcceptance(string Document, string Version, DateTimeOffset AcceptedAt);
