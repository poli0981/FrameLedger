# Header-only: the shared-memory contract between the Overlay (inside the game)
# and the Agent (outside it). Exactly one definition, no link dependency.
add_library(FrameLedger.Shm INTERFACE)
target_include_directories(FrameLedger.Shm INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(FrameLedger.Shm INTERFACE cxx_std_20)
