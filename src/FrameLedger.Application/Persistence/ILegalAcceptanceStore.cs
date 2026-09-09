namespace FrameLedger.Application.Persistence;

/// <summary>
/// READ-ONLY view of <c>legal_acceptance</c>. The UI writes it (FR-11, P3); nothing in the Agent does.
/// </summary>
/// <remarks>
/// Declared now so that making the Legal Gate a precondition of consent (owner decision D8 in HANDOFF §P2:
/// not in P2) is one <c>if</c> at the consent store when the owner turns it on — not a new port.
/// </remarks>
public interface ILegalAcceptanceStore
{
    ValueTask<LegalAcceptance?> FindAsync(string document, CancellationToken ct = default);
}
