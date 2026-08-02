// FrameLedger.Overlay — the injected DLL.
//
// Scaffold. docs/17_HOOK_ENGINE.md is the specification; the constraints that
// shape every line written here:
//
//   - DllMain(DLL_PROCESS_ATTACH) does ONLY DisableThreadLibraryCalls and
//     CreateThread -> InitThread. Loader-lock rules. All real work (dummy
//     device creation, MinHook init, hook installation, shm creation) happens
//     on the init thread.
//   - Every hook body is SEH-guarded, allocation-free, lock-free, and logs
//     nothing. Three faults => self-disable and go dormant.
//   - Never __try around the call to the original function — only around our
//     own code, so we never mask a game bug as ours or vice versa.
//   - Read nothing but the arguments of APIs we hooked (CLAUDE.md rule 4).
//
// Two build-profile questions block real hook code and are recorded in
// docs/20_OPEN_QUESTIONS.md: /guard:cf vs MinHook trampolines (H1), and
// -D_HAS_EXCEPTIONS=0 vs <atomic> (H3).

#include <fl_shm.h>

// Exports keep their real names. Being identifiable to anti-cheat is a
// requirement, not an accident (docs/19_SAFETY_AND_ANTICHEAT.md).
extern "C" {

__declspec(dllexport) unsigned int FlGetLayoutVersion() {
    return FL_SHM_LAYOUT_VERSION;
}

}    // extern "C"
