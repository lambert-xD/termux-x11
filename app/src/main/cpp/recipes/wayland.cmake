# wayland.cmake — Android NDK recipe for Wayland core libraries
# Builds libwayland-server and libwayland-client from submodule.

set(WAYLAND_SRC "${CMAKE_CURRENT_SOURCE_DIR}/wayland/wayland/src")
set(WAYLAND_PROTOCOL "${CMAKE_CURRENT_SOURCE_DIR}/wayland/wayland/protocol")

# Extract version from meson.build
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/wayland/wayland/meson.build" MESON_BUILD)
string(REGEX MATCH "version[ ]*:[ ]*'([0-9]+\\.[0-9]+\\.[0-9]+)'" _ "${MESON_BUILD}")
set(WAYLAND_VERSION "${CMAKE_MATCH_1}")
if(NOT WAYLAND_VERSION)
    set(WAYLAND_VERSION "1.25.90")
endif()
string(REPLACE "." ";" VERSION_LIST ${WAYLAND_VERSION})
list(GET VERSION_LIST 0 WAYLAND_VERSION_MAJOR)
list(GET VERSION_LIST 1 WAYLAND_VERSION_MINOR)
list(GET VERSION_LIST 2 WAYLAND_VERSION_MICRO)

# Generated version header
file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/wayland-version.h"
    CONTENT "#ifndef WAYLAND_VERSION_H\n#define WAYLAND_VERSION_H\n\n#define WAYLAND_VERSION_MAJOR ${WAYLAND_VERSION_MAJOR}\n#define WAYLAND_VERSION_MINOR ${WAYLAND_VERSION_MINOR}\n#define WAYLAND_VERSION_MICRO ${WAYLAND_VERSION_MICRO}\n#define WAYLAND_VERSION \"${WAYLAND_VERSION}\"\n\n#endif\n")

# Android NDK config.h — feature detection for API 26+
# HAVE_MEMFD_CREATE is 0 because memfd_create requires API 30+;
# wayland-os.c falls back to syscall or ASharedMemory.
file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/wayland-config.h"
    CONTENT "#ifndef WAYLAND_CONFIG_H\n#define WAYLAND_CONFIG_H\n\n#define PACKAGE \"wayland\"\n#define PACKAGE_VERSION \"${WAYLAND_VERSION}\"\n#define HAVE_SYS_PRCTL_H 1\n#define HAVE_POSIX_FALLOCATE 0\n#define HAVE_MEMFD_CREATE 0\n#define HAVE_STRNDUP 1\n#define HAVE_ACCEPT4 1\n#define HAVE_MREMAP 0\n#define HAVE_MKOSTEMP 0\n#define HAVE_BROKEN_MSG_CMSG_CLOEXEC 0\n#define _POSIX_C_SOURCE 200809L\n\n#endif\n")

# Generate core Wayland protocol headers (wayland-server.h needs wayland-server-protocol.h)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
set(WAYLAND_CORE_XML "${WAYLAND_PROTOCOL}/wayland.xml")
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/wayland-server-protocol.h"
    COMMAND ${WAYLAND_SCANNER} server-header ${WAYLAND_CORE_XML} ${CMAKE_CURRENT_BINARY_DIR}/wayland-server-protocol.h
    DEPENDS ${WAYLAND_CORE_XML}
    COMMENT "Generating wayland-server-protocol.h"
    VERBATIM)
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/wayland-client-protocol.h"
    COMMAND ${WAYLAND_SCANNER} client-header ${WAYLAND_CORE_XML} ${CMAKE_CURRENT_BINARY_DIR}/wayland-client-protocol.h
    DEPENDS ${WAYLAND_CORE_XML}
    COMMENT "Generating wayland-client-protocol.h"
    VERBATIM)
add_custom_target(wayland-core-protocol-headers DEPENDS
    "${CMAKE_CURRENT_BINARY_DIR}/wayland-server-protocol.h"
    "${CMAKE_CURRENT_BINARY_DIR}/wayland-client-protocol.h")

# wayland-util (static)
add_library(wayland-util STATIC "${WAYLAND_SRC}/wayland-util.c")
add_dependencies(wayland-util wayland-core-protocol-headers)
target_include_directories(wayland-util PRIVATE "${WAYLAND_SRC}" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(wayland-util PRIVATE -fvisibility=hidden -include${CMAKE_CURRENT_BINARY_DIR}/wayland-config.h)

# wayland-os (object library for shared OS code)
add_library(wayland-os OBJECT "${WAYLAND_SRC}/wayland-os.c")
add_dependencies(wayland-os wayland-core-protocol-headers)
target_include_directories(wayland-os PRIVATE "${WAYLAND_SRC}" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(wayland-os PRIVATE -fvisibility=hidden -include${CMAKE_CURRENT_BINARY_DIR}/wayland-config.h)

# wayland-server (static)
add_library(wayland-server STATIC
    "${WAYLAND_SRC}/wayland-server.c"
    "${WAYLAND_SRC}/wayland-shm.c"
    "${WAYLAND_SRC}/event-loop.c"
    "${WAYLAND_SRC}/connection.c"
    $<TARGET_OBJECTS:wayland-os>)
add_dependencies(wayland-server wayland-core-protocol-headers)
target_include_directories(wayland-server PUBLIC "${WAYLAND_SRC}" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(wayland-server PRIVATE -fvisibility=hidden -include${CMAKE_CURRENT_BINARY_DIR}/wayland-config.h -DWL_HIDE_DEPRECATED)
target_link_libraries(wayland-server PUBLIC wayland-util)

# wayland-client (static)
add_library(wayland-client STATIC
    "${WAYLAND_SRC}/wayland-client.c"
    $<TARGET_OBJECTS:wayland-os>)
add_dependencies(wayland-client wayland-core-protocol-headers)
target_include_directories(wayland-client PUBLIC "${WAYLAND_SRC}" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(wayland-client PRIVATE -fvisibility=hidden -include${CMAKE_CURRENT_BINARY_DIR}/wayland-config.h -DWL_HIDE_DEPRECATED)
target_link_libraries(wayland-client PUBLIC wayland-util)

# wayland-util (static)
add_library(wayland-util STATIC "${WAYLAND_SRC}/wayland-util.c")
target_include_directories(wayland-util PRIVATE "${WAYLAND_SRC}" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(wayland-util PRIVATE -fvisibility=hidden -include${CMAKE_CURRENT_BINARY_DIR}/wayland-config.h)

# wayland-os (object library for shared OS code)
add_library(wayland-os OBJECT "${WAYLAND_SRC}/wayland-os.c")
target_include_directories(wayland-os PRIVATE "${WAYLAND_SRC}" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(wayland-os PRIVATE -fvisibility=hidden -include${CMAKE_CURRENT_BINARY_DIR}/wayland-config.h)

# wayland-server (static)
add_library(wayland-server STATIC
    "${WAYLAND_SRC}/wayland-server.c"
    "${WAYLAND_SRC}/wayland-shm.c"
    "${WAYLAND_SRC}/event-loop.c"
    "${WAYLAND_SRC}/connection.c"
    $<TARGET_OBJECTS:wayland-os>)
target_include_directories(wayland-server PUBLIC "${WAYLAND_SRC}" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(wayland-server PRIVATE -fvisibility=hidden -include${CMAKE_CURRENT_BINARY_DIR}/wayland-config.h -DWL_HIDE_DEPRECATED)
target_link_libraries(wayland-server PUBLIC wayland-util)

# wayland-client (static)
add_library(wayland-client STATIC
    "${WAYLAND_SRC}/wayland-client.c"
    $<TARGET_OBJECTS:wayland-os>)
target_include_directories(wayland-client PUBLIC "${WAYLAND_SRC}" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(wayland-client PRIVATE -fvisibility=hidden -include${CMAKE_CURRENT_BINARY_DIR}/wayland-config.h -DWL_HIDE_DEPRECATED)
target_link_libraries(wayland-client PUBLIC wayland-util)
