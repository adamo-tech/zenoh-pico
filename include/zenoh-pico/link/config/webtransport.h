#ifndef ZENOH_PICO_LINK_CONFIG_WEBTRANSPORT_H
#define ZENOH_PICO_LINK_CONFIG_WEBTRANSPORT_H

#include "zenoh-pico/collections/intmap.h"
#include "zenoh-pico/collections/string.h"
#include "zenoh-pico/config.h"

#if Z_FEATURE_LINK_WEBTRANSPORT == 1
#define WEBTRANSPORT_CONFIG_TOUT_KEY 0x01
#define WEBTRANSPORT_CONFIG_TOUT_STR "tout"
#define WEBTRANSPORT_CONFIG_MAPPING_BUILD             \
    uint8_t argc = 1;                                 \
    _z_str_intmapping_t args[argc];                   \
    args[0]._key = WEBTRANSPORT_CONFIG_TOUT_KEY;      \
    args[0]._str = WEBTRANSPORT_CONFIG_TOUT_STR;

size_t _z_webtransport_config_strlen(const _z_str_intmap_t *s);
char *_z_webtransport_config_to_str(const _z_str_intmap_t *s);
z_result_t _z_webtransport_config_from_strn(_z_str_intmap_t *strint, const char *s, size_t n);
#endif
#endif
