// FrameLedger.Agent — capture orchestrator.
//
// Still a scaffold. The real entry point builds a Generic Host containing the
// process watcher, the anti-cheat guard, the injector control, the shm drain,
// the telemetry poller and the session recorder (docs/04_CAPTURE.md).
//
// Ordering constraint from CLAUDE.md rule 2 and docs/15_ROADMAP.md: the guard
// and its full test suite ship BEFORE the first real injection, not after.
// Nothing here may open a process with CREATE_THREAD | VM_WRITE until the
// safety-guard tests in docs/14_TESTING.md are green.
//
// CLI flags this must eventually accept (docs/12_BUILD.md §Debugging):
//   --serve --console --diag --install-task --uninstall-task
//   --register-vklayer --unregister-vklayer
//
// It has ONE real behaviour today: seeding the rules file (§S20). The Agent is
// the sole owner of %LOCALAPPDATA%\FrameLedger (§S18 blocker 3, ratified) and
// therefore the only thing that may write there.

using FrameLedger.Application.Rules;
using FrameLedger.Infrastructure.Rules;

namespace FrameLedger.Agent;

internal static class Program
{
    private static async Task<int> Main(string[] args)
    {
        ArgumentNullException.ThrowIfNull(args);

        // Before anything else. The guard reads this file on every evaluation and
        // refuses every title when it is absent, so a capture started before the
        // seed lands would fail for a reason that has nothing to do with the game.
        RulesSeedOutcome seeded = await new RulesSeeder(new FileSystemRulesStore())
            .EnsureSeededAsync()
            .ConfigureAwait(false);

        // Console, not a logger, because there is no host to configure one in yet.
        // The alternative is a branch whose result goes nowhere, and an outcome
        // nobody can observe is the shape §S20 exists to complain about.
        Console.WriteLine($"rules: {Describe(seeded)}");

        return seeded is RulesSeedOutcome.WriteFailed or RulesSeedOutcome.PackagedSeedUnusable ? 1 : 0;
    }

    private static string Describe(RulesSeedOutcome outcome) => outcome switch
    {
        RulesSeedOutcome.Installed => "installed the packaged blocklist",
        RulesSeedOutcome.Updated => "updated the blocklist this build ships",
        RulesSeedOutcome.AlreadyCurrent => "already current",
        RulesSeedOutcome.ForeignLeftAlone =>
            "a usable rules file is installed that we did not write — left alone (it can only ADD to the blocklist)",
        RulesSeedOutcome.ReplacedUnusable =>
            "the installed rules file was not usable by the guard and has been replaced",
        RulesSeedOutcome.RaceLost => "another process installed it first",
        RulesSeedOutcome.PackagedSeedUnusable =>
            "FAILED — the seed shipped in this build is not usable by the guard; nothing was installed",
        RulesSeedOutcome.WriteFailed => "FAILED — could not write the rules file",
        _ => outcome.ToString(),
    };
}
