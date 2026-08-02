# Vulkan implicit layer. Vulkan titles use this INSTEAD of injection — it is the
# mechanism Khronos supports and far more robust than hooking dispatch tables.
#
# A layer is machine-wide by nature. docs/20_OPEN_QUESTIONS.md §S2 is unresolved
# and blocks this target: the layer currently has no anti-cheat guard and no
# mid-session unhook path, and it loads before anything of ours runs.
add_library(FrameLedger.VkLayer SHARED src/layer.cpp)
target_link_libraries(FrameLedger.VkLayer PRIVATE FrameLedger.Shm fl_native_flags fl_hostile_env_flags)
set_target_properties(FrameLedger.VkLayer PROPERTIES OUTPUT_NAME "FrameLedger.VkLayer")

# Same requirement as the Overlay, and arguably sharper here: an implicit layer
# is machine-wide, so it can appear inside processes the user never associated
# with FrameLedger. It had better say who it is (docs/19_SAFETY).
fl_add_version_resource(FrameLedger.VkLayer "FrameLedger Vulkan layer (VK_LAYER_FRAMELEDGER_overlay)")
