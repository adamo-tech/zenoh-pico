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
    let endpoint = UTF8ToString(url);
    let transport = null;
    let timeoutId = null;
    try {
        // Resolve immediately before every physical connection attempt. Zenoh-
        // Pico may reopen a link internally, so embedding a one-use credential
        // in the configured locator would replay it on reconnect.
        if (Module.zenohPicoResolveWebTransportUrl) {
            endpoint = await Module.zenohPicoResolveWebTransportUrl(endpoint);
            if (typeof endpoint !== 'string' || !endpoint.startsWith('https://')) {
                throw new TypeError('WebTransport URL resolver must return an https:// URL');
            }
        }
        const options = {};
        const hashBase64 = Module.zenohPicoServerCertificateHash;
        if (hashBase64) {
            const raw = atob(hashBase64);
            options.serverCertificateHashes = [{
                algorithm: 'sha-256',
                value: Uint8Array.from(raw, c => c.charCodeAt(0)),
            }];
        }
        transport = new WebTransport(endpoint, options);
        const timeout = new Promise((_, reject) => {
            timeoutId = setTimeout(
                () => reject(new Error('WebTransport open timeout')),
                timeout_ms);
        });
        await Promise.race([transport.ready, timeout]);
        const handle = registry.next++;
        registry.links.set(handle, {
            owner: Module,
            transport,
            streamPromise: transport.createBidirectionalStream(),
            reader: null,
            writer: null,
            incoming: [],
            incomingOffset: 0,
            incomingBytes: 0,
            closed: false,
            writeError: false,
            readPump: null,
            dataSignal: null,
            dataSignalResolve: null,
            maxIncomingBytes: Math.max(65536, Module.zenohPicoReceiveBufferBytes || 8 * 1024 * 1024),
            spaceSignal: null,
            spaceSignalResolve: null,
            stats: {readCalls: 0, readBytes: 0, writeCalls: 0, writeBytes: 0},
        });
        return handle;
    } catch (error) {
        // A failed resolver or handshake must not leave a transport or a
        // rejecting timer alive after this Asyncify call has returned.
        if (transport) {
            try { transport.close(); } catch (_) {}
        }
        if (Module.printErr) Module.printErr('WebTransport open failed: ' + String(error));
        return -1;
    } finally {
        if (timeoutId !== null) clearTimeout(timeoutId);
    }
});

EM_ASYNC_JS(int, _zp_webtransport_js_read, (int handle, uint8_t *dst, size_t len, uint32_t timeout_ms), {
    const link = globalThis.__zenohPicoWebTransport?.links.get(handle);
    if (!link) return -1;
    if (link.incomingBytes === 0 && !link.closed) {
        if (!link.dataSignal) {
            link.dataSignal = new Promise(resolve => { link.dataSignalResolve = resolve; });
        }
        const timeout = new Promise(resolve => setTimeout(resolve, timeout_ms));
        await Promise.race([link.dataSignal, timeout]);
    }
    if (link.incomingBytes === 0) return link.closed ? -1 : 0;
    const wanted = Math.min(len, link.incomingBytes);
    let copied = 0;
    while (copied < wanted) {
        const chunk = link.incoming[0];
        const available = chunk.length - link.incomingOffset;
        const count = Math.min(wanted - copied, available);
        HEAPU8.set(chunk.subarray(link.incomingOffset, link.incomingOffset + count), dst + copied);
        copied += count;
        link.incomingOffset += count;
        link.incomingBytes -= count;
        if (link.incomingOffset === chunk.length) {
            link.incoming.shift();
            link.incomingOffset = 0;
        }
    }
    if (link.spaceSignalResolve && link.incomingBytes < link.maxIncomingBytes / 2) {
        link.spaceSignalResolve();
        link.spaceSignal = null;
        link.spaceSignalResolve = null;
    }
    link.stats.readCalls++;
    link.stats.readBytes += copied;
    return copied;
});

EM_JS(int, _zp_webtransport_js_read_nonblocking, (int handle, uint8_t *dst, size_t len), {
    const link = globalThis.__zenohPicoWebTransport?.links.get(handle);
    if (!link) return -1;
    if (link.incomingBytes === 0) return link.closed ? -1 : 0;
    const wanted = Math.min(len, link.incomingBytes);
    let copied = 0;
    while (copied < wanted) {
        const chunk = link.incoming[0];
        const available = chunk.length - link.incomingOffset;
        const count = Math.min(wanted - copied, available);
        HEAPU8.set(chunk.subarray(link.incomingOffset, link.incomingOffset + count), dst + copied);
        copied += count;
        link.incomingOffset += count;
        link.incomingBytes -= count;
        if (link.incomingOffset === chunk.length) {
            link.incoming.shift();
            link.incomingOffset = 0;
        }
    }
    if (link.spaceSignalResolve && link.incomingBytes < link.maxIncomingBytes / 2) {
        link.spaceSignalResolve();
        link.spaceSignal = null;
        link.spaceSignalResolve = null;
    }
    link.stats.readCalls++;
    link.stats.readBytes += copied;
    return copied;
});

EM_ASYNC_JS(int, _zp_webtransport_js_write, (int handle, const uint8_t *src, size_t len), {
    const link = globalThis.__zenohPicoWebTransport?.links.get(handle);
    if (!link) return -1;
    try {
        if (!link.writer) {
            const stream = await link.streamPromise;
            link.reader = stream.readable.getReader();
            link.writer = stream.writable.getWriter();
            link.readPump = (async () => {
                try {
                    for (;;) {
                        if (link.incomingBytes >= link.maxIncomingBytes) {
                            if (!link.spaceSignal) {
                                link.spaceSignal = new Promise(resolve => { link.spaceSignalResolve = resolve; });
                            }
                            await link.spaceSignal;
                        }
                        const result = await link.reader.read();
                        if (result.done) break;
                        if (result.value?.length) {
                            link.incoming.push(result.value);
                            link.incomingBytes += result.value.length;
                            if (Module.onZenohTransportData) {
                                Module.onZenohTransportData(handle, result.value.length);
                            }
                            if (link.dataSignalResolve) link.dataSignalResolve();
                            link.dataSignal = null;
                            link.dataSignalResolve = null;
                        }
                    }
                } catch (_) {
                    // The transport's closed promise carries the detailed error.
                } finally {
                    link.closed = true;
                    if (Module.onZenohTransportData) Module.onZenohTransportData(handle, 0);
                    if (link.dataSignalResolve) link.dataSignalResolve();
                    if (link.spaceSignalResolve) link.spaceSignalResolve();
                }
            })();
            link.transport.closed.catch(() => {}).finally(() => {
                link.closed = true;
                if (link.dataSignalResolve) link.dataSignalResolve();
                if (link.spaceSignalResolve) link.spaceSignalResolve();
            });
        }
        if (link.closed || link.writeError) return -1;
        const bytes = HEAPU8.slice(src, src + len);
        // Await acceptance so Pico never observes a successful reliable write
        // that the browser has already rejected.
        await link.writer.write(bytes);
        link.stats.writeCalls++;
        link.stats.writeBytes += len;
        return len;
    } catch (error) {
        link.lastError = String(error);
        link.writeError = true;
        link.closed = true;
        return -1;
    }
});

EM_ASYNC_JS(void, _zp_webtransport_js_close, (int handle), {
    const registry = globalThis.__zenohPicoWebTransport;
    const link = registry?.links.get(handle);
    if (!link) return;
    try {
        if (link.writer) {
            await Promise.race([
                link.writer.close(),
                new Promise(resolve => setTimeout(resolve, 1000)),
            ]);
        }
    } catch (_) {}
    if (link.reader) await link.reader.cancel().catch(() => {});
    try { link.transport.close(); } catch (_) {}
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
    int result = _zp_webtransport_js_read_nonblocking(sock._webtransport._handle, ptr, len);
    return result < 0 ? SIZE_MAX : (size_t)result;
}

EMSCRIPTEN_KEEPALIVE
size_t _z_webtransport_transport_read(const _z_webtransport_socket_t *sock, uint8_t *ptr, size_t len) {
    return _z_webtransport_read(sock->_sock, ptr, len);
}

size_t _z_webtransport_transport_read_exact(const _z_webtransport_socket_t *sock, uint8_t *ptr, size_t len) {
    size_t total = 0;
    while (total < len) {
        int result = _zp_webtransport_js_read(sock->_sock._webtransport._handle, _z_ptr_u8_offset(ptr, total),
                                              len - total, sock->_sock._webtransport._tout);
        size_t count = result < 0 ? SIZE_MAX : (size_t)result;
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
