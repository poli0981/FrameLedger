# Stub DLLs carrying MEASURED vendor export names, for the hook fixtures.
#
# NOT SHIPPED, and structurally so: 12_BUILD publishes FrameLedger.App and
# FrameLedger.Agent only, neither references anything here, and these are built
# under FL_BUILD_TOOLS. They exist so `hook-harness` can put a module named
# sl.interposer.dll into a process and see whether the Overlay finds it.
#
# THE OUTPUT NAMES ARE THE VENDOR'S; THE VERSION BLOCKS ARE OURS. A file called
# sl.interposer.dll that announced itself as NVIDIA's would be a lie sitting in
# our build tree, and 19_SAFETY's whole posture is that our binaries say plainly
# what they are. fl_add_version_resource is given the real output filename so the
# resource matches the file rather than the CMake target name.
#
# They are NOT added to tools/versioninfo-check.ps1's shipped-binary list: that
# gate asserts the three binaries we ship carry a version block, and a fixture is
# not one of them.

function(fl_add_vendor_stub target output source)
    add_library(${target} SHARED ${source})
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "${output}")
    # fl_hook_inventory.h (via fl_dxgi_vtable's sibling include dir) binds the
    # exported name to the Overlay's; fl_streamline_headers supplies the vendor
    # signature so the stub and the detour cannot disagree about the ABI.
    target_link_libraries(${target} PRIVATE FrameLedger.Shm fl_native_flags fl_streamline_headers)
    target_include_directories(${target} PRIVATE
        "${CMAKE_SOURCE_DIR}/FrameLedger.Overlay/include"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    fl_add_version_resource(${target} "FrameLedger test fixture - not a vendor library" "${output}.dll")
endfunction()

# The module the inventory names. Its slEvaluateFeature is the one that must be
# hooked.
fl_add_vendor_stub(fl_stub_sl_interposer "sl.interposer" stub_sl_interposer.cpp)

# The decoy: same symbol, different module, must NOT be hooked. Without it,
# "module-scoped resolution" is a property no test can falsify.
fl_add_vendor_stub(fl_stub_sl_common "sl.common" stub_sl_common.cpp)

# The WRONG-GENERATION fixture: right module name, right symbol name, Streamline
# 1.x ABI. Must NOT be hooked either, for a different reason than the decoy —
# scoping cannot see it, because the module name is correct.
#
# ITS OWN SUBDIRECTORY, and that is forced rather than tidy: its output name is
# also `sl.interposer.dll`, so it would collide with the SL2 stub in a shared
# output directory. Separate directories also mean a test can load exactly one of
# them by absolute path, which is what makes the two cases independent —
# GetModuleHandleExW resolves by name, so a process holding both would answer
# whichever loaded first and the fixtures would silently test each other.
fl_add_vendor_stub(fl_stub_sl_interposer_v1 "sl.interposer" stub_sl_interposer_v1.cpp)
# ARCHIVE_ too, and it is not optional: on Windows a SHARED library emits the DLL
# under RUNTIME_ and the IMPORT LIBRARY under ARCHIVE_. Redirecting only the first
# two leaves both targets generating tools/vendor-stubs/sl.interposer.lib, and
# ninja refuses the configure with "multiple rules generate" -- which is the
# generator catching a collision the DLL rename hid.
set_target_properties(fl_stub_sl_interposer_v1 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/sl1"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/sl1"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/sl1")
