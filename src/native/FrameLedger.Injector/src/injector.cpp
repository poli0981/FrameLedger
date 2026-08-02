// FrameLedger.Injector — launch/attach injection and the anti-cheat guard probe.
//
// Scaffold. Nothing in this file may open a process with
// CREATE_THREAD | VM_OPERATION | VM_WRITE until the guard and its full test
// suite exist (CLAUDE.md rule 2; docs/14_TESTING.md §Safety-guard tests;
// docs/15_ROADMAP.md "it ships before the first real injection, not after").
//
// Injection uses documented LoadLibraryW. No manual mapping, no PE header
// erasure, no PEB unlinking, no thread hiding — ever. A performance tool must
// be visible to anti-cheat, not hidden from it (docs/19_SAFETY §What we will
// never build).
//
// Open safety question blocking this target: docs/20_OPEN_QUESTIONS.md §S1 —
// the guard's module scan is a no-op against a CREATE_SUSPENDED process,
// because a suspended process has loaded no modules yet, and launch mode is
// the preferred path.

#include <fl_shm.h>

namespace fl::injector {

// Placeholder so the target links. Real API arrives in P1, behind the guard.
unsigned int LayoutVersion() noexcept {
    return FL_SHM_LAYOUT_VERSION;
}

}    // namespace fl::injector
