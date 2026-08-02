# Vulkan implicit layer. Vulkan titles use this INSTEAD of injection — it is the
# mechanism Khronos supports and far more robust than hooking dispatch tables.
#
# A layer is machine-wide by nature, so it carries its own gate: the enable
# variable and enable-list decide whether it may observe anything, and the §S2
# blocklist self-scan below decides whether it must stay inert regardless.
# Nothing is intercepted yet either way — presentation hooks are P1.
#
# fl_ac_rules.cpp is compiled IN, not linked from FrameLedger.Injector: the
# layer must not drag the injection primitive into a game process. Same source
# file, so the layer and the guard share ONE matcher and ONE rules path — a
# layer with its own blocklist would be a second matcher that can disagree.
add_library(FrameLedger.VkLayer SHARED
    src/layer.cpp
    ../FrameLedger.Injector/src/fl_ac_rules.cpp
    FrameLedger.VkLayer.def
)
target_include_directories(FrameLedger.VkLayer PRIVATE ../FrameLedger.Injector/include)

# Vendored headers, not the SDK: CI must not need a ~1 GB install, and these are
# the exact headers matching the loader the blast-radius test runs against.
# See src/native/third_party/vulkan-headers/README.md.
target_link_libraries(FrameLedger.VkLayer PRIVATE
    FrameLedger.Shm fl_native_flags fl_hostile_env_flags fl_vulkan_headers fl_jsmn)
set_target_properties(FrameLedger.VkLayer PROPERTIES OUTPUT_NAME "FrameLedger.VkLayer")

# Same requirement as the Overlay, and arguably sharper here: an implicit layer
# is machine-wide, so it can appear inside processes the user never associated
# with FrameLedger. It had better say who it is (docs/19_SAFETY).
fl_add_version_resource(FrameLedger.VkLayer "FrameLedger Vulkan layer (VK_LAYER_FRAMELEDGER_overlay)")

# ---------------------------------------------------------------------------
# The manifest. docs/12_BUILD.md listed this as a build output from the start;
# it did not exist anywhere in the repository until now.
#
# Generated with the absolute path of the built DLL so the manifest in the build
# tree is directly usable by the blast-radius test WITHOUT installing anything.
# The shipped manifest is written at install time with the installed path
# (12_BUILD §Bundled assets) — and is NOT registered then (§S10).
#
# enable_environment is the §S2 fix: the loader will not map this layer at all
# unless FRAMELEDGER_ENABLE_VK_LAYER=1 is set, which the Agent does only when
# launching a game the user opted in. That makes Vulkan Tier 1 launch-mode-only,
# and it is a LOADING gate rather than a security one — anything running as the
# user can set the variable.
# ---------------------------------------------------------------------------
set(FL_VKLAYER_LIBRARY_PATH "$<TARGET_FILE:FrameLedger.VkLayer>")
file(GENERATE
    OUTPUT "$<TARGET_FILE_DIR:FrameLedger.VkLayer>/VkLayer_FRAMELEDGER_overlay.json"
    INPUT "${CMAKE_CURRENT_BINARY_DIR}/VkLayer_FRAMELEDGER_overlay.json.configured"
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/VkLayer_FRAMELEDGER_overlay.json.in"
    "${CMAKE_CURRENT_BINARY_DIR}/VkLayer_FRAMELEDGER_overlay.json.configured"
    @ONLY
)
