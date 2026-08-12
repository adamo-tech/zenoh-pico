#include "zenoh-pico/config.h"

#if Z_FEATURE_LINK_WEBTRANSPORT == 1
#include <stdlib.h>

#include "zenoh-pico/link/config/webtransport.h"
#include "zenoh-pico/link/manager.h"
#include "zenoh-pico/link/transport/webtransport.h"

z_result_t _z_endpoint_webtransport_valid(_z_endpoint_t *endpoint) {
    _z_string_t schema = _z_string_alias_str(WEBTRANSPORT_SCHEMA);
    if (!_z_string_equals(&endpoint->_locator._protocol, &schema) ||
        _z_string_len(&endpoint->_locator._address) == 0) {
        return _Z_ERR_CONFIG_LOCATOR_INVALID;
    }
    return _Z_RES_OK;
}

static z_result_t _z_f_link_open_webtransport(_z_link_t *zl) {
    uint32_t tout = Z_CONFIG_SOCKET_TIMEOUT;
    char *value = _z_str_intmap_get(&zl->_endpoint._config, WEBTRANSPORT_CONFIG_TOUT_KEY);
    if (value != NULL) tout = (uint32_t)strtoul(value, NULL, 10);
    return _z_webtransport_transport_open(&zl->_socket._webtransport, tout);
}
static z_result_t _z_f_link_listen_webtransport(_z_link_t *zl) {
    _ZP_UNUSED(zl);
    return _Z_ERR_GENERIC;
}
static void _z_f_link_close_webtransport(_z_link_t *zl) {
    _z_webtransport_transport_close(&zl->_socket._webtransport);
}
static void _z_f_link_free_webtransport(_z_link_t *zl) {
    _z_webtransport_endpoint_clear(&zl->_socket._webtransport._rep);
}
static size_t _z_f_link_write_webtransport(const _z_link_t *zl, const uint8_t *ptr, size_t len,
                                            _z_sys_net_socket_t *socket) {
    _ZP_UNUSED(socket);
    return _z_webtransport_transport_write(&zl->_socket._webtransport, ptr, len);
}
static size_t _z_f_link_write_all_webtransport(const _z_link_t *zl, const uint8_t *ptr, size_t len) {
    return _z_webtransport_transport_write(&zl->_socket._webtransport, ptr, len);
}
static size_t _z_f_link_read_webtransport(const _z_link_t *zl, uint8_t *ptr, size_t len, _z_slice_t *addr) {
    _ZP_UNUSED(addr);
    return _z_webtransport_transport_read(&zl->_socket._webtransport, ptr, len);
}
static size_t _z_f_link_read_exact_webtransport(const _z_link_t *zl, uint8_t *ptr, size_t len, _z_slice_t *addr,
                                                 _z_sys_net_socket_t *socket) {
    _ZP_UNUSED(addr);
    _ZP_UNUSED(socket);
    return _z_webtransport_transport_read_exact(&zl->_socket._webtransport, ptr, len);
}
static size_t _z_f_link_webtransport_read_socket(const _z_sys_net_socket_t socket, uint8_t *ptr, size_t len) {
    return _z_webtransport_transport_read_socket(socket, ptr, len);
}

z_result_t _z_new_link_webtransport(_z_link_t *zl, _z_endpoint_t *endpoint) {
    zl->_type = _Z_LINK_TYPE_WEBTRANSPORT;
    zl->_cap._transport = Z_LINK_CAP_TRANSPORT_UNICAST;
    zl->_cap._flow = Z_LINK_CAP_FLOW_STREAM;
    zl->_cap._is_reliable = true;
    zl->_mtu = 65535;
    zl->_endpoint = *endpoint;
    z_result_t ret = _z_webtransport_endpoint_init(&zl->_socket._webtransport._rep, &endpoint->_locator._address);
    zl->_open_f = _z_f_link_open_webtransport;
    zl->_listen_f = _z_f_link_listen_webtransport;
    zl->_close_f = _z_f_link_close_webtransport;
    zl->_free_f = _z_f_link_free_webtransport;
    zl->_write_f = _z_f_link_write_webtransport;
    zl->_write_all_f = _z_f_link_write_all_webtransport;
    zl->_read_f = _z_f_link_read_webtransport;
    zl->_read_exact_f = _z_f_link_read_exact_webtransport;
    zl->_read_socket_f = _z_f_link_webtransport_read_socket;
    return ret;
}
#endif
