# Header-only: the shared-memory contract between the Overlay (inside the game)
# and the Agent (outside it). Exactly one definition, no link dependency.
add_library(FrameLedger.Shm INTERFACE)
target_include_directories(FrameLedger.Shm INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(FrameLedger.Shm INTERFACE cxx_std_20)

# FL_BUILD_ID — the producer FlShmHandshake::buildId never had.
#
# 07_IPC and 04_CAPTURE make a buildId mismatch a hard refuse-to-attach, and
# 17_HOOK_ENGINE requires an FlGetBuildId() export. Nothing wrote the field:
# grep found the declaration, its offset assert and the layout dump, and nothing
# else. A check whose input nobody produces compares "" with "" forever, which is
# a gate that cannot fail wearing a version-skew costume.
#
# git describe, because the question the field answers is "which build is this
# DLL" and a commit answers it exactly. Falls back to a literal rather than to an
# empty string: "unknown" is a value the mismatch check can act on, "" is the
# state that made the check vacuous.
#
# Deliberately NOT a timestamp. Two builds of the same commit must compare equal,
# or every rebuild would tell the user to restart their game.
#
# KNOWN LIMIT, stated rather than discovered: this runs at CONFIGURE time, so the
# `-dirty` suffix reflects the tree when CMake last ran, not when the DLL was
# last compiled. For a released build that is exact — CI configures and builds
# once from a clean checkout. For a developer editing between configures it is
# approximate, and the field is a version-skew signal rather than a provenance
# guarantee. Making it exact means re-running git on every build, which is a cost
# on every incremental compile for a value only the attach handshake reads.
execute_process(
    COMMAND git describe --always --dirty --abbrev=12
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE FL_BUILD_ID_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT FL_BUILD_ID_RAW)
    set(FL_BUILD_ID_RAW "unknown")
endif()
# buildId is char[32] and the guard side compares the whole array, so a value
# that would be truncated must be caught here rather than becoming a permanent
# mismatch nobody can explain.
string(LENGTH "${FL_BUILD_ID_RAW}" FL_BUILD_ID_LEN)
if(FL_BUILD_ID_LEN GREATER 31)
    message(FATAL_ERROR
        "FL_BUILD_ID '${FL_BUILD_ID_RAW}' is ${FL_BUILD_ID_LEN} chars; FlShmHandshake::buildId holds 31 + NUL. "
        "Truncating it would make every attach fail on a mismatch that is really this build system's fault.")
endif()
target_compile_definitions(FrameLedger.Shm INTERFACE FL_BUILD_ID="${FL_BUILD_ID_RAW}")
message(STATUS "FL_BUILD_ID = ${FL_BUILD_ID_RAW}")
