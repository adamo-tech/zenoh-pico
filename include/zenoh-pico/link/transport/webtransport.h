#ifndef ZENOH_PICO_LINK_TRANSPORT_WEBTRANSPORT_H
#define ZENOH_PICO_LINK_TRANSPORT_WEBTRANSPORT_H

#include "zenoh-pico/config.h"
#include "zenoh-pico/collections/string.h"
#include "zenoh-pico/system/platform.h"
#include "zenoh-pico/utils/result.h"

#if Z_FEATURE_LINK_WEBTRANSPORT == 1
typedef struct {
    _z_sys_net_socket_t _sock;
    _z_sys_net_endpoint_t _rep;
} _z_webtransport_socket_t;

z_result_t _z_webtransport_endpoint_init(_z_sys_net_endpoint_t *ep, const _z_string_t *address);
void _z_webtransport_endpoint_clear(_z_sys_net_endpoint_t *ep);
z_result_t _z_webtransport_transport_open(_z_webtransport_socket_t *sock, uint32_t tout);
void _z_webtransport_transport_close(_z_webtransport_socket_t *sock);
size_t _z_webtransport_transport_read(const _z_webtransport_socket_t *sock, uint8_t *ptr, size_t len);
size_t _z_webtransport_transport_read_exact(const _z_webtransport_socket_t *sock, uint8_t *ptr, size_t len);
size_t _z_webtransport_transport_write(const _z_webtransport_socket_t *sock, const uint8_t *ptr, size_t len);
size_t _z_webtransport_transport_read_socket(const _z_sys_net_socket_t socket, uint8_t *ptr, size_t len);
#endif
#endif
