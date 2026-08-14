set(ZP_PLATFORM_SYSTEM_LAYER emscripten)
set(ZP_PLATFORM_COMPILE_DEFINITIONS ZENOH_EMSCRIPTEN)
set(CHECK_THREADS OFF)
set(ZP_PLATFORM_SOURCE_FILES
    "${PROJECT_SOURCE_DIR}/src/system/emscripten/system.c"
    "${PROJECT_SOURCE_DIR}/src/system/emscripten/network.c"
    "${PROJECT_SOURCE_DIR}/src/link/transport/upper/ws_emscripten.c"
    "${PROJECT_SOURCE_DIR}/src/link/transport/upper/webtransport_emscripten.c")
