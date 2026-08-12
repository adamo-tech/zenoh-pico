#include <emscripten/emscripten.h>
#include <stdio.h>
#include <string.h>

#include <zenoh-pico.h>

EMSCRIPTEN_KEEPALIVE
int zenoh_pico_wasm_smoke(void) {
    static const char keyexpr[] = "demo/example";
    return z_keyexpr_is_canon(keyexpr, strlen(keyexpr));
}

int main(void) {
    int result = zenoh_pico_wasm_smoke();
    printf("zenoh-pico WASM smoke test: %s (%d)\n", result == 0 ? "PASS" : "FAIL", result);
    return result == 0 ? 0 : 1;
}
