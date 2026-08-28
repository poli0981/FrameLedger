# Measures whether the anti-cheat guard's SIGNER HALF can be built in policy
# (docs/20_OPEN_QUESTIONS.md §S19(b), docs/spike-notes.md §1).
#
# NOT the guard, and deliberately not a step towards one: every subject here is
# a FILE PATH, so this probe opens no process at all. Should a later revision
# need one, it takes PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ — the
# rights docs/19_SAFETY_AND_ANTICHEAT.md specifies for the module scan — and
# never CREATE_THREAD | VM_OPERATION | VM_WRITE, for the reason fl-probe-guard
# states beside the same rule.
#
# fl_native_flags only, NOT fl_hostile_env_flags: this runs in our own process,
# not inside a host game, so the -D_HAS_EXCEPTIONS=0 / hook-path rules that
# shape the Overlay do not apply. wintrust and crypt32 are linked by #pragma in
# the source, matching fl-probe-guard's advapi32 — and they are named here as a
# reminder that they must NEVER reach fl_ac_rules.cpp, which the Vulkan layer
# compiles into a DLL mapped by every Vulkan process on the machine.
add_executable(fl-probe-signer main.cpp)
target_link_libraries(fl-probe-signer PRIVATE fl_native_flags)
target_compile_definitions(fl-probe-signer PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# Exit 0 alone cannot distinguish "reached an answer" from `int main(){return 0;}`,
# and this probe's whole acceptance criterion is that it PRINTS answers (#86's
# precedent: four printed answers, not four passing asserts). The alternation is
# load-bearing: a machine with no .NET shared framework must not be red for being
# itself, and an unrun leg is reported as unrun rather than as a negative.
add_test(NAME fl_signer_probe COMMAND fl-probe-signer)
set_tests_properties(fl_signer_probe PROPERTIES
                     PASS_REGULAR_EXPRESSION "BLOCKER: (FOUND|NOT PRESENT)")
