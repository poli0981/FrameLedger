// FrameLedger.Agent — capture orchestrator.
//
// Scaffold only. The real entry point builds a Generic Host containing the
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

namespace FrameLedger.Agent;

internal static class Program
{
    private static int Main(string[] args)
    {
        ArgumentNullException.ThrowIfNull(args);
        return 0;
    }
}
