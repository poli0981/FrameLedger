using System.Security.Cryptography;

namespace FrameLedger.Application.Rules;

/// <summary>
/// Puts a usable rules file where the guard reads one (<c>20_OPEN_QUESTIONS</c> §S20).
/// </summary>
/// <remarks>
/// <para>
/// Before this existed, nothing in the repository ever wrote that file, so on any
/// machine that had not hand-installed one the guard answered
/// <c>RulesUnreadable</c> for every title. That is correct fail-closed behaviour
/// and also the whole story: the first real injection's opening refusal was
/// exactly this.
/// </para>
/// <para>
/// <strong>What makes the decisions below safe is the floor, not this class.</strong>
/// Since the compiled-in blocklist is generated from the shipped seed, a rules
/// file can only ADD to what the guard blocks — it cannot remove a family, a
/// value or a name fragment (<c>19_SAFETY</c> §The floor data cannot remove). So
/// "leave a file we did not install alone" is not an override; at worst it is a
/// stale extension set. Under the narrow floor this class shipped against
/// originally, the same rule would have handed permanent control of the blocklist
/// to whoever created the file first.
/// </para>
/// <para>
/// <strong>Residual, stated rather than discovered:</strong> the one
/// allow-widening field a foreign file still controls is
/// <c>anticheat.heuristic.trustedSigners</c>, which is deliberately not floored
/// because flooring an allowlist has the wrong polarity. It is inert today —
/// <c>IsTrustedSigner</c> has no production call site while §S19(b) is deferred —
/// and it becomes live the moment the signer half is wired. That is a
/// prerequisite of §S19(b), recorded here because this is the class that made a
/// foreign file reachable.
/// </para>
/// <para>
/// <strong>This is the seed half only.</strong> There is no fetch client and no
/// network call; <c>05_DETECTION</c> §Trust and staleness describes an HTTPS feed
/// that remains unbuilt, so FR-7.3's independent anti-cheat update schedule is
/// still unmet.
/// </para>
/// </remarks>
public sealed class RulesSeeder(IRulesStore store)
{
    private readonly IRulesStore _store = store ?? throw new ArgumentNullException(nameof(store));

    /// <summary>Ensure a usable rules file exists, and keep our own copy current.</summary>
    public async ValueTask<RulesSeedOutcome> EnsureSeededAsync(CancellationToken ct = default)
    {
        byte[] packaged = await _store.ReadPackagedSeedAsync(ct).ConfigureAwait(false);

        // Our own artifact is checked first, and the check is not ceremonial: it
        // is the difference between "install nothing and say why" and "install a
        // document the guard then refuses for every title on the machine".
        if (!_store.IsUsableByTheGuard(packaged))
        {
            return RulesSeedOutcome.PackagedSeedUnusable;
        }

        byte[]? installed = await _store.ReadInstalledAsync(ct).ConfigureAwait(false);

        if (installed is null)
        {
            RulesWriteOutcome wrote = await _store.WriteAsync(packaged, replaceExisting: false, ct)
                .ConfigureAwait(false);
            return wrote switch
            {
                RulesWriteOutcome.Written => RulesSeedOutcome.Installed,
                RulesWriteOutcome.AlreadyExists => RulesSeedOutcome.RaceLost,
                _ => RulesSeedOutcome.WriteFailed,
            };
        }

        string installedHash = Sha256(installed);
        if (string.Equals(installedHash, Sha256(packaged), StringComparison.Ordinal))
        {
            return RulesSeedOutcome.AlreadyCurrent;
        }

        // Not usable ⇒ replace, whoever wrote it. There is no information to
        // clobber: the guard already refuses every title on this file, and
        // nothing else in the product repairs it, so leaving it would make a
        // truncated or corrupt file a permanent machine-wide refusal.
        if (!_store.IsUsableByTheGuard(installed))
        {
            return await ReplaceAsync(packaged, RulesSeedOutcome.ReplacedUnusable, ct).ConfigureAwait(false);
        }

        // Usable and ours ⇒ this build ships a different seed, so update it. This
        // is the drift §S20 records — a new binary and a year-old blocklist
        // coexisting forever — closed by provenance rather than by comparing
        // `rulesVersion`, which this repository's own history shows nobody bumps
        // when the blocklist changes.
        string? marker = await _store.ReadInstalledMarkerAsync(ct).ConfigureAwait(false);
        if (marker is not null && string.Equals(marker, installedHash, StringComparison.OrdinalIgnoreCase))
        {
            return await ReplaceAsync(packaged, RulesSeedOutcome.Updated, ct).ConfigureAwait(false);
        }

        // Usable and not ours. Left alone deliberately — see the remarks above.
        return RulesSeedOutcome.ForeignLeftAlone;
    }

    private async ValueTask<RulesSeedOutcome> ReplaceAsync(byte[] packaged, RulesSeedOutcome onSuccess,
        CancellationToken ct)
    {
        RulesWriteOutcome wrote = await _store.WriteAsync(packaged, replaceExisting: true, ct).ConfigureAwait(false);
        return wrote == RulesWriteOutcome.Written ? onSuccess : RulesSeedOutcome.WriteFailed;
    }

    private static string Sha256(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes));
}
