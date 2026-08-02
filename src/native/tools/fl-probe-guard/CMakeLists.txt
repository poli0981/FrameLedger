# Measures the Windows APIs the anti-cheat guard is built on
# (docs/spike-notes.md §1, docs/20_OPEN_QUESTIONS.md §S1 and §S7).
#
# NOT the guard, and deliberately not a step towards one: it opens processes
# only with PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ — the exact
# rights docs/19_SAFETY_AND_ANTICHEAT.md specifies for the module scan — and
# never with CREATE_THREAD | VM_OPERATION | VM_WRITE. A probe that quietly took
# more rights than the guard gets would answer a question the guard never asks.
#
# fl_native_flags only, NOT fl_hostile_env_flags: the guard runs in our own
# process, not inside a host game, so the -D_HAS_EXCEPTIONS=0 / hook-path rules
# that shape the Overlay do not apply here.
add_executable(fl-probe-guard main.cpp)
target_link_libraries(fl-probe-guard PRIVATE fl_native_flags)
target_compile_definitions(fl-probe-guard PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# Runs unelevated on a developer machine and on a hosted CI runner alike: no
# GPU, no window station, no admin. The one branch it cannot reach either way
# is ACCESS_DENIED on a service, which the probe reports as NOT MEASURED
# rather than leaving to be assumed.
add_test(NAME fl_guard_apis COMMAND fl-probe-guard)
