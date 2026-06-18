cmake_minimum_required(VERSION 3.13)

project(inkstation C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# PocketBook apps are plain ELF binaries renamed to <name>.app and dropped on
# the SD card under applications/. Build the binary as "inkstation.app" so the
# artifact is install-ready. Cross-compile with the bundled toolchain file:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-obreey.cmake \
#         -DPB_SDK_ROOT=/path/to/SDK-B288
add_executable(inkstation.app
    src/main.c
    src/app.c
    src/ui.c
    src/config.c
    src/screens.c
    src/rtt.c
    src/cJSON.c
    src/http.c
    src/net.c
)

target_include_directories(inkstation.app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Embed the build version for the about/footer line: the latest semantic-version
# tag (v1.0, v1.1, ...), plus "-<n>-g<hash>" when ahead of that tag, else a bare
# short hash before the first tag exists. Only v* tags count. A "dev" fallback
# covers builds outside a git checkout (e.g. the host test gate).
execute_process(
    COMMAND git describe --tags --always --match "v*"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_VERSION)
    set(GIT_VERSION "dev")
endif()
target_compile_definitions(inkstation.app PRIVATE INKSTATION_VERSION="${GIT_VERSION}")

# libcurl provides HTTP for the RTT token exchange and board queries. Shipped in
# the PocketBook SDK toolchain; find it via the sysroot, fall back to -lcurl.
find_package(CURL QUIET)
if(CURL_FOUND)
    target_link_libraries(inkstation.app PRIVATE CURL::libcurl)
else()
    find_library(CURL_LIB NAMES curl HINTS ${PB_SDK_ROOT}/lib ${PB_SYSROOT}/usr/lib)
    if(CURL_LIB)
        target_link_libraries(inkstation.app PRIVATE ${CURL_LIB})
    else()
        message(WARNING "libcurl not found; linking with -lcurl")
        target_link_libraries(inkstation.app PRIVATE curl)
    endif()
endif()

# InkView is the PocketBook UI/runtime library. When building inside the SDK
# image it is found via the sysroot; allow an explicit hint otherwise.
find_library(INKVIEW_LIB
    NAMES inkview
    HINTS ${PB_SDK_ROOT}/lib ${PB_SYSROOT}/usr/lib
)

if(INKVIEW_LIB)
    target_link_libraries(inkstation.app PRIVATE ${INKVIEW_LIB})
else()
    # Fall back to a bare -linkview so configuration still succeeds on a host
    # without the SDK present (e.g. for editing / static analysis).
    message(WARNING "libinkview not found via find_library; linking with -linkview")
    target_link_libraries(inkstation.app PRIVATE inkview)
endif()

# Some toolchains need libm explicitly for the math used by libcurl/inkview.
target_link_libraries(inkstation.app PRIVATE m)

# No "lib" prefix, keep the .app suffix exactly.
set_target_properties(inkstation.app PROPERTIES
    PREFIX ""
    SUFFIX ""
    OUTPUT_NAME "inkstation.app"
)
