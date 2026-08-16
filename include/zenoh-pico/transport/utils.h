//
// Copyright (c) 2022 ZettaScale Technology
//
// This program and the accompanying materials are made available under the
// terms of the Eclipse Public License 2.0 which is available at
// http://www.eclipse.org/legal/epl-2.0, or the Apache License, Version 2.0
// which is available at https://www.apache.org/licenses/LICENSE-2.0.
//
// SPDX-License-Identifier: EPL-2.0 OR Apache-2.0
//
// Contributors:
//   ZettaScale Zenoh Team, <zenoh@zettascale.tech>
//

#ifndef ZENOH_PICO_TRANSPORT_UTILS_H
#define ZENOH_PICO_TRANSPORT_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zenoh-pico/config.h"
#include "zenoh-pico/protocol/core.h"
#include "zenoh-pico/protocol/definitions/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/*------------------ SN helpers ------------------*/
#ifndef Z_BEST_EFFORT_REORDER_WINDOW
#define Z_BEST_EFFORT_REORDER_WINDOW 256
#endif

#if Z_BEST_EFFORT_REORDER_WINDOW < 1
#error "Z_BEST_EFFORT_REORDER_WINDOW must be at least 1"
#endif

#define _Z_SN_WINDOW_WORD_BITS 64
#define _Z_SN_WINDOW_WORDS ((Z_BEST_EFFORT_REORDER_WINDOW + _Z_SN_WINDOW_WORD_BITS - 1) / _Z_SN_WINDOW_WORD_BITS)

typedef enum {
    _Z_SN_WINDOW_AHEAD,
    _Z_SN_WINDOW_REORDERED,
    _Z_SN_WINDOW_DUPLICATE,
    _Z_SN_WINDOW_TOO_OLD,
} _z_sn_window_result_t;

typedef struct {
    _z_zint_t _high_water;
    uint64_t _seen[_Z_SN_WINDOW_WORDS];
} _z_sn_window_t;

_z_zint_t _z_sn_max(uint8_t bits);
_z_zint_t _z_sn_half(_z_zint_t sn);
_z_zint_t _z_sn_modulo_mask(uint8_t bits);
bool _z_sn_precedes(const _z_zint_t sn_resolution, const _z_zint_t sn_left, const _z_zint_t sn_right);
bool _z_sn_consecutive(const _z_zint_t sn_resolution, const _z_zint_t sn_left, const _z_zint_t sn_right);
_z_zint_t _z_sn_increment(const _z_zint_t sn_resolution, const _z_zint_t sn);
_z_zint_t _z_sn_decrement(const _z_zint_t sn_resolution, const _z_zint_t sn);
void _z_sn_window_reset(_z_sn_window_t *window, _z_zint_t high_water);
_z_sn_window_result_t _z_sn_window_observe(_z_sn_window_t *window, _z_zint_t sn_resolution, _z_zint_t value);

void _z_conduit_sn_list_copy(_z_conduit_sn_list_t *dst, const _z_conduit_sn_list_t *src);
void _z_conduit_sn_list_decrement(const _z_zint_t sn_resolution, _z_conduit_sn_list_t *sns);

#ifdef __cplusplus
}
#endif

#endif /* ZENOH_PICO_TRANSPORT_UTILS_H */
