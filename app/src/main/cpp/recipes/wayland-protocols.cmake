# wayland-protocols.cmake — Generate C headers from Wayland protocol XMLs

set(WAYLAND_PROTOCOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/wayland/wayland-protocols")
set(WAYLAND_PROTOCOL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/wayland/wayland/protocol")

find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
message(STATUS "wayland-scanner: ${WAYLAND_SCANNER}")

# Core wayland protocol (needed by generated code)
set(WAYLAND_CORE_PROTOCOL "${WAYLAND_PROTOCOL_DIR}/wayland.xml")

function(wayland_protocol_generate PROTOCOL_XML OUT_PREFIX)
    get_filename_component(PROTOCOL_NAME "${PROTOCOL_XML}" NAME_WE)
    set(OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols")
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")

    set(HEADER "${OUTPUT_DIR}/${OUT_PREFIX}.h")
    set(PRIVATE_CODE "${OUTPUT_DIR}/${OUT_PREFIX}-private.c")
    set(CLIENT_HEADER "${OUTPUT_DIR}/${OUT_PREFIX}-client.h")

    add_custom_command(
        OUTPUT "${HEADER}"
        COMMAND ${WAYLAND_SCANNER} server-header ${PROTOCOL_XML} ${HEADER}
        DEPENDS ${PROTOCOL_XML}
        COMMENT "Generating Wayland server header for ${PROTOCOL_NAME}"
        VERBATIM)

    add_custom_command(
        OUTPUT "${PRIVATE_CODE}"
        COMMAND ${WAYLAND_SCANNER} private-code ${PROTOCOL_XML} ${PRIVATE_CODE}
        DEPENDS ${PROTOCOL_XML}
        COMMENT "Generating Wayland server code for ${PROTOCOL_NAME}"
        VERBATIM)

    add_custom_command(
        OUTPUT "${CLIENT_HEADER}"
        COMMAND ${WAYLAND_SCANNER} client-header ${PROTOCOL_XML} ${CLIENT_HEADER}
        DEPENDS ${PROTOCOL_XML}
        COMMENT "Generating Wayland client header for ${PROTOCOL_NAME}"
        VERBATIM)

    set(WAYLAND_PROTOCOL_HEADERS ${WAYLAND_PROTOCOL_HEADERS} ${HEADER} PARENT_SCOPE)
    set(WAYLAND_PROTOCOL_SOURCES ${WAYLAND_PROTOCOL_SOURCES} ${PRIVATE_CODE} PARENT_SCOPE)
    set(WAYLAND_PROTOCOL_CLIENT_HEADERS ${WAYLAND_PROTOCOL_CLIENT_HEADERS} ${CLIENT_HEADER} PARENT_SCOPE)
endfunction()

# Stable protocols required by the compositor
set(STABLE_PROTOCOLS
    "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml"
    "${WAYLAND_PROTOCOLS_DIR}/stable/viewporter/viewporter.xml"
    "${WAYLAND_PROTOCOLS_DIR}/stable/presentation-time/presentation-time.xml"
    "${WAYLAND_PROTOCOLS_DIR}/stable/linux-dmabuf/linux-dmabuf-v1.xml")

foreach(PROTOCOL ${STABLE_PROTOCOLS})
    if(EXISTS ${PROTOCOL})
        get_filename_component(PROTOCOL_DIR ${PROTOCOL} DIRECTORY)
        get_filename_component(PROTOCOL_DIR_NAME ${PROTOCOL_DIR} NAME)
        get_filename_component(PROTOCOL_PARENT_DIR ${PROTOCOL_DIR} DIRECTORY)
        get_filename_component(PROTOCOL_PARENT_NAME ${PROTOCOL_PARENT_DIR} NAME)
        get_filename_component(PROTOCOL_NAME ${PROTOCOL} NAME_WE)
        wayland_protocol_generate(${PROTOCOL} "${PROTOCOL_PARENT_NAME}-${PROTOCOL_DIR_NAME}-${PROTOCOL_NAME}")
    endif()
endforeach()

# Staging protocols
set(STAGING_PROTOCOLS
    "${WAYLAND_PROTOCOLS_DIR}/staging/xwayland-shell/xwayland-shell-v1.xml")

foreach(PROTOCOL ${STAGING_PROTOCOLS})
    if(EXISTS ${PROTOCOL})
        get_filename_component(PROTOCOL_DIR ${PROTOCOL} DIRECTORY)
        get_filename_component(PROTOCOL_DIR_NAME ${PROTOCOL_DIR} NAME)
        get_filename_component(PROTOCOL_PARENT_DIR ${PROTOCOL_DIR} DIRECTORY)
        get_filename_component(PROTOCOL_PARENT_NAME ${PROTOCOL_PARENT_DIR} NAME)
        get_filename_component(PROTOCOL_NAME ${PROTOCOL} NAME_WE)
        wayland_protocol_generate(${PROTOCOL} "${PROTOCOL_PARENT_NAME}-${PROTOCOL_DIR_NAME}-${PROTOCOL_NAME}")
    endif()
endforeach()

# Ensure headers are generated before any compilation that includes them
add_custom_target(wayland-protocols-headers DEPENDS ${WAYLAND_PROTOCOL_HEADERS})

# Interface library for consumers that only need include paths
add_library(wayland-protocols INTERFACE)
target_include_directories(wayland-protocols INTERFACE
    "${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols"
    "${WAYLAND_PROTOCOLS_DIR}/include")
add_dependencies(wayland-protocols wayland-protocols-headers)

# Object library with generated implementation sources
add_library(wayland-protocols-generated OBJECT ${WAYLAND_PROTOCOL_SOURCES})
add_dependencies(wayland-protocols-generated wayland-protocols-headers)
target_include_directories(wayland-protocols-generated PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols"
    "${WAYLAND_SRC}"
    "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(wayland-protocols-generated PUBLIC wayland-server)
