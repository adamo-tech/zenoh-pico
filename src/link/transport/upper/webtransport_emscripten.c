#include "zenoh-pico/config.h"

#if defined(ZENOH_EMSCRIPTEN) && Z_FEATURE_LINK_WEBTRANSPORT == 1

#include <emscripten.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "zenoh-pico/link/transport/webtransport.h"
#include "zenoh-pico/utils/pointers.h"

EM_ASYNC_JS(int, _zp_webtransport_js_open, (const char *url, uint32_t timeout_ms), {
    if (typeof WebTransport === 'undefined') return -1;
    if (!globalThis.__zenohPicoWebTransport) {
        globalThis.__zenohPicoWebTransport = { next: 1, links: new Map() };
    }
    const registry = globalThis.__zenohPicoWebTransport;
    const endpoint = UTF8ToString(url);
    const timeout = new Promise((_, reject) =>
        setTimeout(() => reject(new Error('WebTransport open timeout')), timeout_ms));
    try {
        const transport = new WebTransport(endpoint);
        await Promise.race([transport.ready, timeout]);
        const stream = await Promise.race([transport.createBidirectionalStream(), timeout]);
        const handle = registry.next++;
        registry.links.set(handle, {
            transport,
            reader: stream.readable.getReader(),
            writer: stream.writable.getWriter(),
            pending: new Uint8Array(0),
        });
        return handle;
    } catch (_) {
        return -1;
    }
});

EM_ASYNC_JS(int, _zp_webtransport_js_read, (int handle, uint8_t *dst, size_t len, uint32_t timeout_ms), {
    const link = globalThis.__zenohPicoWebTransport?.links.get(handle);
    if (!link) return -1;
    try {
        if (link.pending.length === 0) {
            const timeout = new Promise(resolve => setTimeout(() => resolve(null), timeout_ms));
            const result = await Promise.race([link.reader.read(), timeout]);
            if (result === null) return 0;
            if (result.done) return -1;
            link.pending = result.value;
        }
        const count = Math.min(len, link.pending.length);
        HEAPU8.set(link.pending.subarray(0, count), dst);
        link.pending = link.pending.subarray(count);
        return count;
    } catch (_) {
        return -1;
    }
});

EM_ASYNC_JS(int, _zp_webtransport_js_write, (int handle, const uint8_t *src, size_t len), {
    const link = globalThis.__zenohPicoWebTransport?.links.get(handle);
    if (!link) return -1;
    try {
        const bytes = HEAPU8.slice(src, src + len);
        await link.writer.write(bytes);
        return len;
    } catch (_) {
        return -1;
    }
});

EM_JS(void, _zp_webtransport_js_close, (int handle), {
    const registry = globalThis.__zenohPicoWebTransport;
    const link = registry?.links.get(handle);
    if (!link) return;
    link.reader.cancel().catch(() => {});
    link.writer.close().catch(() => {});
    link.transport.close();
    registry.links.delete(handle);
});

z_result_t _z_webtransport_endpoint_init(_z_sys_net_endpoint_t *ep, const _z_string_t *address) {
    size_t len = _z_string_len(address);
    const char *data = _z_string_data(address);
    bool has_scheme = len >= 8 && memcmp(data, "https://", 8) == 0;
    size_t prefix = has_scheme ? 0 : 8;
    ep->_webtransport_url = (char *)z_malloc(prefix + len + 1);
    if (ep->_webtransport_url == NULL) return _Z_ERR_SYSTEM_OUT_OF_MEMORY;
    if (!has_scheme) memcpy(ep->_webtransport_url, "https://", 8);
    memcpy(ep->_webtransport_url + prefix, data, len);
    ep->_webtransport_url[prefix + len] = '\0';
    return _Z_RES_OK;
}

void _z_webtransport_endpoint_clear(_z_sys_net_endpoint_t *ep) {
    z_free(ep->_webtransport_url);
    ep->_webtransport_url = NULL;
}

EMSCRIPTEN_KEEPALIVE
z_result_t _z_webtransport_transport_open(_z_webtransport_socket_t *sock, uint32_t tout) {
    sock->_sock._webtransport._tout = tout;
    sock->_sock._webtransport._handle = _zp_webtransport_js_open(sock->_rep._webtransport_url, tout);
    return sock->_sock._webtransport._handle < 0 ? _Z_ERR_TRANSPORT_OPEN_FAILED : _Z_RES_OK;
}

void _z_webtransport_transport_close(_z_webtransport_socket_t *sock) {
    _zp_webtransport_js_close(sock->_sock._webtransport._handle);
    sock->_sock._webtransport._handle = -1;
}

static size_t _z_webtransport_read(_z_sys_net_socket_t sock, uint8_t *ptr, size_t len) {
    int result = _zp_webtransport_js_read(sock._webtransport._handle, ptr, len, sock._webtransport._tout);
    return result < 0 ? SIZE_MAX : (size_t)result;
}

EMSCRIPTEN_KEEPALIVE
size_t _z_webtransport_transport_read(const _z_webtransport_socket_t *sock, uint8_t *ptr, size_t len) {
    return _z_webtransport_read(sock->_sock, ptr, len);
}

size_t _z_webtransport_transport_read_exact(const _z_webtransport_socket_t *sock, uint8_t *ptr, size_t len) {
    size_t total = 0;
    while (total < len) {
        size_t count = _z_webtransport_read(sock->_sock, _z_ptr_u8_offset(ptr, total), len - total);
        if (count == 0 || count == SIZE_MAX) return count;
        total += count;
    }
    return total;
}

EMSCRIPTEN_KEEPALIVE
size_t _z_webtransport_transport_write(const _z_webtransport_socket_t *sock, const uint8_t *ptr, size_t len) {
    int result = _zp_webtransport_js_write(sock->_sock._webtransport._handle, ptr, len);
    return result < 0 ? SIZE_MAX : (size_t)result;
}

size_t _z_webtransport_transport_read_socket(const _z_sys_net_socket_t socket, uint8_t *ptr, size_t len) {
    return _z_webtransport_read(socket, ptr, len);
}
#else
typedef int _zp_webtransport_emscripten_transport_disabled_t;
#endif
