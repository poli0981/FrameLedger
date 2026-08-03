# The measurement baseline P0 item 4 compares hooks against (15_ROADMAP item 3).
#
# Links FrameLedger.Injector for ONE thing: fl::guard::SystemSources()'s module
# enumerator. That walk is the measured, fail-closed one (LIST_MODULES_ALL, per
# spike-notes §1), and a probe with its own copy would drift from what the
# product actually uses. Nothing here reaches the injection primitive — the
# chokepoint check proves that over every native source, this file included.
add_executable(fl-baseline-probe main.cpp)
target_link_libraries(fl-baseline-probe PRIVATE FrameLedger.Injector fl_native_flags fl_jsmn)
target_compile_definitions(fl-baseline-probe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
    # The capability list, read from the repository seed by absolute path at
    # compile time.
    #
    # NOT the %LOCALAPPDATA% lookup fl-probe-vklayer uses. That probe exercises
    # the layer, which reads the product's one rules location and has no
    # override (§S3). This one asks a different question — what does the
    # capability data IN THIS COMMIT describe — so reading a file some other
    # build left on the machine would answer the wrong question.
    FL_SEED_RULES="${CMAKE_SOURCE_DIR}/../../rules/detection-rules.json"

    # What --self-test plants, renamed to look like a capability module.
    # OUR OWN DLL (14_TESTING §Integration tests): no vendor binary is shipped,
    # downloaded or executed, and nothing here needs one to be installed.
    FL_PLANT_SOURCE=L"$<TARGET_FILE:FrameLedger.Guard>"
)
add_dependencies(fl-baseline-probe FrameLedger.Guard)

# BOTH directions, or the test proves nothing: a probe that never reports
# "loaded" would sail through a one-sided check. --self-test asserts a clean
# process reports nothing, a planted module IS detected, and the answer flips
# back after unload rather than latching.
add_test(NAME fl_baseline_probe COMMAND fl-baseline-probe --self-test)
