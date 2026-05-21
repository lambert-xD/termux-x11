# Minimal libffi stub for Android NDK build verification.
# TODO: replace with real libffi from source/submodule.

add_library(ffi STATIC "libffi/ffi.c")
target_include_directories(ffi PUBLIC "libffi")
