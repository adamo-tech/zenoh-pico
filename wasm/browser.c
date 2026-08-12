#include <emscripten/emscripten.h>
#include <stdbool.h>
#include <stdio.h>

#include <zenoh-pico.h>

static z_owned_session_t session;
static bool session_open = false;

void _z_webtransport_transport_set_nonblocking(bool enabled);

EMSCRIPTEN_KEEPALIVE
int zenoh_pico_browser_open(const char *endpoint) {
    if (session_open) return 0;
    _z_webtransport_transport_set_nonblocking(false);
    z_owned_config_t config;
    z_config_default(&config);
    if (zp_config_insert(z_loan_mut(config), Z_CONFIG_MODE_KEY, "client") < 0 ||
        zp_config_insert(z_loan_mut(config), Z_CONFIG_CONNECT_KEY, endpoint) < 0) {
        z_drop(z_move(config));
        return -1;
    }
    int result = z_open(&session, z_move(config), NULL);
    session_open = result >= 0;
    if (session_open) _z_webtransport_transport_set_nonblocking(true);
    return result;
}

EMSCRIPTEN_KEEPALIVE
int zenoh_pico_browser_put(const char *keyexpr, const char *value) {
    if (!session_open) return -1;
    z_view_keyexpr_t key;
    if (z_view_keyexpr_from_str(&key, keyexpr) < 0) return -2;
    z_owned_bytes_t payload;
    z_bytes_from_static_str(&payload, value);
    return z_put(z_loan(session), z_loan(key), z_move(payload), NULL);
}

EMSCRIPTEN_KEEPALIVE
int zenoh_pico_browser_put_batch(const char *keyexpr, const char *value, int count) {
    if (!session_open || count < 1) return -1;
    z_view_keyexpr_t key;
    if (z_view_keyexpr_from_str(&key, keyexpr) < 0) return -2;
    int result = zp_batch_start(z_loan(session));
    if (result < 0) return result;
    int sent = 0;
    for (; sent < count; sent++) {
        z_owned_bytes_t payload;
        z_bytes_from_static_str(&payload, value);
        result = z_put(z_loan(session), z_loan(key), z_move(payload), NULL);
        if (result < 0) break;
    }
    int stop_result = zp_batch_stop(z_loan(session));
    if (result < 0) return result;
    return stop_result < 0 ? stop_result : sent;
}

EMSCRIPTEN_KEEPALIVE
int zenoh_pico_browser_poll(void) {
    if (!session_open) return -1;
    return zp_spin_once(z_loan(session)) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
void zenoh_pico_browser_close(void) {
    if (!session_open) return;
    z_drop(z_move(session));
    session_open = false;
}

int main(void) { return 0; }
