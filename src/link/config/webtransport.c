#include "zenoh-pico/link/config/webtransport.h"

#if Z_FEATURE_LINK_WEBTRANSPORT == 1
size_t _z_webtransport_config_strlen(const _z_str_intmap_t *s) {
    WEBTRANSPORT_CONFIG_MAPPING_BUILD
    return _z_str_intmap_strlen(s, argc, args);
}
char *_z_webtransport_config_to_str(const _z_str_intmap_t *s) {
    WEBTRANSPORT_CONFIG_MAPPING_BUILD
    return _z_str_intmap_to_str(s, argc, args);
}
z_result_t _z_webtransport_config_from_strn(_z_str_intmap_t *strint, const char *s, size_t n) {
    WEBTRANSPORT_CONFIG_MAPPING_BUILD
    return _z_str_intmap_from_strn(strint, s, argc, args, n);
}
#endif
