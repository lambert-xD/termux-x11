# CMakeLists.txt for Lorie Wayland Compositor
# This file builds the Wayland compositor as part of the existing CMake system

cmake_minimum_required(VERSION 3.22)

# Source files for the Wayland compositor
set(LORIE_WAYLAND_SOURCES
    main.c
    compositor.c
    output.c
    surface.c
    seat.c
    renderer.c
    input.c
    wayland-activity.c
    xwayland.c
    protocols/xdg-shell.c
    protocols/linux-dmabuf.c
    protocols/wl-data-device-manager.c
    protocols/relative-pointer.c
    protocols/pointer-constraints.c
)

# Create a static library for the Wayland compositor
# This can be linked into libXlorie.so or built as a separate library
add_library(lorie-wayland STATIC ${LORIE_WAYLAND_SOURCES})

# Set C standard
set_target_properties(lorie-wayland PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# Include directories
target_include_directories(lorie-wayland PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../lorie
    ${CMAKE_CURRENT_SOURCE_DIR}/../../wayland/wayland/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../wayland/wayland/protocol
    ${CMAKE_CURRENT_BINARY_DIR}/../../wayland-protocols
    ${CMAKE_CURRENT_SOURCE_DIR}/../../wayland/wayland-protocols/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../pixman/pixman
    ${CMAKE_CURRENT_BINARY_DIR}/../../pixman/pixman
)

# Compile definitions
target_compile_definitions(lorie-wayland PRIVATE
    EGL_EGLEXT_PROTOTYPES
    GL_GLEXT_PROTOTYPES
    _GNU_SOURCE
)

# Link libraries
target_link_libraries(lorie-wayland PRIVATE
    # Wayland server library (built from submodule)
    wayland-server
    wayland-protocols-generated

    # Pixman for software rendering
    pixman-1

    # Android native libraries
    android
    log
    EGL
    GLESv2
)

# If building as part of libXlorie.so, the parent CMakeLists.txt should:
# target_sources(Xlorie PRIVATE ${LORIE_WAYLAND_SOURCES})
# target_include_directories(Xlorie PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Optional: Build as shared library for standalone testing
# add_library(LorieWayland SHARED ${LORIE_WAYLAND_SOURCES})
# target_link_libraries(LorieWayland PRIVATE wayland-server pixman-1 android log EGL GLESv2)
