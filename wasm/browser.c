#include <emscripten/emscripten.h>
#include <stdbool.h>
#include <stdio.h>

#include <zenoh-pico.h>

static z_owned_session_t session;
static bool session_open = false;
static z_owned_subscriber_t subscriber;
static bool subscriber_open = false;

void _z_webtransport_transport_set_nonblocking(bool enabled);

EM_JS(void, zenoh_pico_deliver_video_frame, (const uint8_t *data, size_t len), {
    if (Module.onZenohVideoFrame) Module.onZenohVideoFrame(HEAPU8.slice(data, data + len));
});

static void video_sample_handler(z_loaned_sample_t *sample, void *context) {
    (void)context;
    z_owned_slice_t payload;
    if (z_bytes_to_slice(z_sample_payload(sample), &payload) == 0) {
        zenoh_pico_deliver_video_frame(z_slice_data(z_loan(payload)), z_slice_len(z_loan(payload)));
        z_drop(z_move(payload));
    }
}

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
int zenoh_pico_browser_put_binary(const char *keyexpr, const uint8_t *data, size_t len) {
    if (!session_open || data == NULL || len == 0) return -1;
    z_view_keyexpr_t key;
    if (z_view_keyexpr_from_str(&key, keyexpr) < 0) return -2;
    z_owned_bytes_t payload;
    int result = z_bytes_copy_from_buf(&payload, data, len);
    if (result < 0) return result;
    z_put_options_t options;
    z_put_options_default(&options);
    options.congestion_control = Z_CONGESTION_CONTROL_DROP;
    options.is_express = true;
    return z_put(z_loan(session), z_loan(key), z_move(payload), &options);
}

EMSCRIPTEN_KEEPALIVE
int zenoh_pico_browser_subscribe_video(const char *keyexpr) {
    if (!session_open) return -1;
    if (subscriber_open) return 0;
    z_view_keyexpr_t key;
    if (z_view_keyexpr_from_str(&key, keyexpr) < 0) return -2;
    z_owned_closure_sample_t callback;
    int result = z_closure_sample(&callback, video_sample_handler, NULL, NULL);
    if (result < 0) return result;
    result = z_declare_subscriber(z_loan(session), &subscriber, z_loan(key), z_move(callback), NULL);
    subscriber_open = result >= 0;
    return result;
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
    int work = 0;
    // Drain a bounded amount of immediately-ready runtime work per JS/WASM
    // crossing. This is especially important when a frame spans multiple
    // Zenoh transport operations.
    while (work < 64 && zp_spin_once(z_loan(session))) work++;
    return work;
}

EMSCRIPTEN_KEEPALIVE
void zenoh_pico_browser_close(void) {
    if (!session_open) return;
    if (subscriber_open) {
        z_drop(z_move(subscriber));
        subscriber_open = false;
    }
    z_drop(z_move(session));
    session_open = false;
}

int main(void) { return 0; }
